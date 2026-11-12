// Price-target alerts (8C): "tell me when AAPL reaches $250" (above) or "falls to $180"
// (below). One-shot: a target fires once, then stays marked as hit until it's set again.
// Pure C++, unit-tested on the PC.
#pragma once
#include <stdint.h>

// Plain data (saved to NVS as raw bytes), so no default member values: brace-initialize
// with PriceTarget{} for "no target".
struct PriceTarget {
  float price;      // 0 = no target
  int32_t hitDay;   // 0 = armed; otherwise the day number it fired (see dayNumber())
};

inline bool targetSet(const PriceTarget& t) { return t.price > 0; }
inline bool targetArmed(const PriceTarget& t) { return t.price > 0 && t.hitDay == 0; }

// Checks one target against a new price. An "above" target fires once the price is at or
// over it, a "below" target once it's at or under it. The editor only accepts targets on
// the far side of the current price, so this is the moment the price crosses it, and it
// still works after a restart (no previous price needed). Returns true when it fires, and
// marks it hit on `today`.
bool checkTarget(PriceTarget& target, bool above, float price, int32_t today);

// Whether a newly typed target makes sense: an "above" target must be over the current
// price, a "below" one under it (price 0 = unknown, then anything positive is accepted).
bool targetValid(float target, bool above, float currentPrice);
