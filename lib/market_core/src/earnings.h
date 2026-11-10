// The next earnings report for one stock, from Finnhub's /calendar/earnings (~185 B).
// Plain data, so it can be saved to flash as raw bytes. Pure C++, unit-tested on the PC.
//
// Example: {"earningsCalendar":[{"symbol":"AMD","date":"2027-02-01","hour":"amc",
//           "epsEstimate":2.9483,...}]}
#pragma once
#include <stdint.h>

struct Earnings {
  bool valid = false;        // fetched (even if no report was found)
  int32_t day = 0;           // day number of the next report; 0 = none in the window
  char hour[4] = {};         // "bmo" before open, "amc" after close, "dmh" during hours, "" unknown
  float epsEstimate = 0;
  int32_t fetchedDay = 0;
};

// Picks the soonest report on or after `today` (a day number). An empty list (ETFs,
// unknown symbols, nothing scheduled yet) still returns true, with day = 0.
// False only for invalid JSON.
bool parseEarnings(const char* json, int32_t today, Earnings& out);

// "before open", "after close", "during day", or "" when the time isn't announced.
const char* earningsTimeText(const Earnings& e);
