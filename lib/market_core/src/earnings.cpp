#include "earnings.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include "market_calendar.h"

bool parseEarnings(const char* json, int32_t today, Earnings& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonArrayConst list = doc["earningsCalendar"];
  if (list.isNull()) return false;   // not an earnings reply at all

  out.day = 0;
  out.hour[0] = '\0';
  out.epsEstimate = 0;
  for (JsonObjectConst e : list) {
    int y, m, d;
    const char* date = e["date"] | "";
    if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3) continue;
    int32_t day = dayNumber(y, m, d);
    if (day < today) continue;                     // already happened
    if (out.day != 0 && day >= out.day) continue;  // keep the soonest
    out.day = day;
    strncpy(out.hour, e["hour"] | "", sizeof(out.hour) - 1);
    out.hour[sizeof(out.hour) - 1] = '\0';
    out.epsEstimate = e["epsEstimate"] | 0.0f;
  }
  return true;
}

const char* earningsTimeText(const Earnings& e) {
  if (strcmp(e.hour, "bmo") == 0) return "before open";
  if (strcmp(e.hour, "amc") == 0) return "after close";
  if (strcmp(e.hour, "dmh") == 0) return "during day";
  return "";
}
