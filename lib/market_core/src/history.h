// Recent price history for one stock, backfilled from Twelve Data (Finnhub's free plan
// has no history). Daily closes give the 1M chart and the 1W/1M change; 30-minute closes
// give the 5D chart. Plain fixed-size arrays, so it can be saved to flash as raw bytes.
// Pure C++, unit-tested on the PC.
#pragma once
#include <stdint.h>

static const int HISTORY_DAYS = 25;   // ~1 month of sessions (21) plus a margin
static const int HISTORY_BARS = 70;   // 13 half-hours per session x 5 sessions, plus a margin

struct History {
  bool valid = false;
  int32_t throughDay = 0;              // last completed session the data includes (day number)
  uint8_t dailyCount = 0;              // oldest first
  int32_t dailyDay[HISTORY_DAYS] = {};
  float dailyClose[HISTORY_DAYS] = {};
  uint8_t barCount = 0;                // oldest first
  uint32_t barTime[HISTORY_BARS] = {}; // unix seconds at the END of each 30-minute bar
  float barClose[HISTORY_BARS] = {};
};

// Twelve Data CSV, newest row first, e.g.
//   datetime;open;high;low;close;volume
//   2026-11-06;658.34998;664.52002;653.98999;656.099976;70469100
// Returns false for an error reply (JSON instead of CSV) or no usable rows.
bool parseDailyCsv(const char* csv, History& out);

// Same, for 30-minute bars requested with timezone=UTC:
//   2026-11-06 20:30:00;737.96002;738.030029;735.07;735.34003;9769519
// A bar's close is the price at its end, so it's stored at start + 30 minutes.
bool parseBarsCsv(const char* csv, History& out);

// Percent change from the close `sessions` trading days before `currentSessionDay`
// to `price`. Rows on or after the current session are ignored (they'd be today).
// False when there isn't enough history yet.
bool changeOverSessions(const History& h, int32_t currentSessionDay, float price, int sessions,
                        float& percent);
