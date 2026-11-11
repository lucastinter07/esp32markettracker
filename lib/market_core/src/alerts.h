// Price alerts: decides when a stock's daily move crosses the user's threshold (±X %).
// Each direction fires at most once per stock per day. Pure C++, unit-tested on the PC.
#pragma once
#include <stdint.h>

enum class AlertDirection : uint8_t { None, Up, Down };

struct AlertState {
  int32_t day = -1;        // day number the flags below belong to
  bool upFired = false;
  bool downFired = false;
};

// changePct: today's change in percent (Finnhub "dp"). thresholdPct 0 = alerts off.
// Returns the direction that just fired, or None.
AlertDirection checkAlert(AlertState& state, float changePct, float thresholdPct, int32_t today);
