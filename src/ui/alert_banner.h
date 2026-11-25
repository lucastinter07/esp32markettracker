// The price-alert banner: a 30 px strip across the top of the Home, Detail or Sectors screen.
//   Price target (8C), target color:  "<diamond> NVDA HIT 132.50 NOW 132.56          | X"
//   Daily move, green / red:          "<up> VRT UP 5.12% TODAY 98.30                 | X"
// Tapping the text opens the stock; the X (or BACK) clears the banner.
#pragma once
#include <stdint.h>
#include "app/price_alerts.h"   // AlertEvent

namespace banner {

static const int16_t HEIGHT = 30;

void draw(const AlertEvent& alert);
bool isTap(int16_t x, int16_t y);        // the text part: open the stock
bool isCloseTap(int16_t x, int16_t y);   // the X: clear it

}  // namespace banner
