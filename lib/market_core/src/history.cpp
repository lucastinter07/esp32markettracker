#include "history.h"
#include <stdio.h>
#include <string.h>
#include "market_calendar.h"

// Moves to the start of the next line; returns nullptr at the end of the text.
static const char* nextLine(const char* p) {
  const char* nl = strchr(p, '\n');
  return nl ? nl + 1 : nullptr;
}

// Reverses the first `n` items of an array (Twelve Data sends newest first).
template <typename T>
static void reverse(T* a, int n) {
  for (int i = 0, j = n - 1; i < j; i++, j--) {
    T tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
  }
}

bool parseDailyCsv(const char* csv, History& out) {
  if (!csv || strncmp(csv, "datetime;", 9) != 0) return false;   // an error reply is JSON
  int n = 0;
  for (const char* line = nextLine(csv); line && *line && n < HISTORY_DAYS; line = nextLine(line)) {
    int y, m, d;
    float close;
    // %*[^;] skips one field (open, high, low); close is the 5th field.
    if (sscanf(line, "%d-%d-%d;%*[^;];%*[^;];%*[^;];%f", &y, &m, &d, &close) == 4 && close > 0) {
      out.dailyDay[n] = dayNumber(y, m, d);
      out.dailyClose[n] = close;
      n++;
    }
  }
  if (n == 0) return false;
  reverse(out.dailyDay, n);
  reverse(out.dailyClose, n);
  out.dailyCount = n;
  return true;
}

int parseBarsCsvInto(const char* csv, uint32_t* times, float* prices, int capacity, uint32_t barSeconds) {
  if (!csv || strncmp(csv, "datetime;", 9) != 0) return 0;
  int n = 0;
  for (const char* line = nextLine(csv); line && *line && n < capacity; line = nextLine(line)) {
    int y, mo, d, h, mi, s;
    float close;
    if (sscanf(line, "%d-%d-%d %d:%d:%d;%*[^;];%*[^;];%*[^;];%f", &y, &mo, &d, &h, &mi, &s, &close) == 7 &&
        close > 0) {
      times[n] = (uint32_t)dayNumber(y, mo, d) * 86400u + h * 3600 + mi * 60 + s + barSeconds;
      prices[n] = close;
      n++;
    }
  }
  reverse(times, n);
  reverse(prices, n);
  return n;
}

bool parseBarsCsv(const char* csv, History& out) {
  int n = parseBarsCsvInto(csv, out.barTime, out.barClose, HISTORY_BARS, 1800);   // 30-minute bars
  if (n == 0) return false;
  out.barCount = n;
  return true;
}

int sessionHalfHours(const PriceSeries& live, const History& h, uint32_t open, uint32_t close, uint32_t* outTimes,
                     float* outPrices, int capacity) {
  const uint32_t HALF = 1800;
  // Live prices only fill half hours that are over (the newest quote is at or past their
  // end): a half hour still in progress would otherwise be drawn at a time still to come.
  uint32_t newest = live.size() > 0 ? live.timeAt(live.size() - 1) : 0;
  int n = 0;
  for (uint32_t end = open + HALF; end <= close && n < capacity; end += HALF) {
    bool found = false;
    float price = 0;
    for (int k = 0; k < h.barCount && !found; k++) {
      // Bar end times sit exactly on :00 / :30; allow a little slack anyway.
      uint32_t t = h.barTime[k];
      if (t + 60 > end && t < end + 60) {
        price = h.barClose[k];
        found = true;
      }
    }
    for (size_t i = 0; i < live.size() && !found && end <= newest; i++) {
      // The last live price in this half hour: scan it all, the latest one wins.
      size_t j = live.size() - 1 - i;
      uint32_t t = live.timeAt(j);
      if (t > end - HALF && t <= end) {
        price = live.at(j);
        found = true;
      }
    }
    if (found) {
      outTimes[n] = end;
      outPrices[n] = price;
      n++;
    }
  }
  // The session in progress: its newest price, after the last full half hour.
  if (live.size() > 0 && n < capacity) {
    uint32_t t = live.timeAt(live.size() - 1);
    if (t > open && t <= close && (n == 0 || t > outTimes[n - 1])) {
      outTimes[n] = t;
      outPrices[n] = live.last();
      n++;
    }
  }
  return n;
}

bool closeSessionsBack(const History& h, int32_t currentSessionDay, int sessions, float& close) {
  // Count the rows from before the current session; the last of them is 1 session back.
  int before = 0;
  while (before < h.dailyCount && h.dailyDay[before] < currentSessionDay) before++;
  if (sessions < 1 || before < sessions) return false;
  close = h.dailyClose[before - sessions];
  return close > 0;
}

bool changeOverSessions(const History& h, int32_t currentSessionDay, float price, int sessions,
                        float& percent) {
  float ref;
  if (price <= 0 || !closeSessionsBack(h, currentSessionDay, sessions, ref)) return false;
  percent = (price - ref) / ref * 100.0f;
  return true;
}
