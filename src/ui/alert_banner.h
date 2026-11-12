// The price-alert banner: a strip across the top of the Home, Detail or Sectors screen.
// Daily move: green (up) or red (down), e.g. "NVDA up 5.24% today, $512.30   TAP TO VIEW".
// Price target (8C): amber, e.g. "AAPL hit 250.00, now $250.12   TAP TO VIEW".
#pragma once
#include <stdint.h>
#include "app/price_alerts.h"   // AlertEvent

namespace banner {

static const int16_t HEIGHT = 30;

void draw(const AlertEvent& alert);
bool isTap(int16_t x, int16_t y);

}  // namespace banner
