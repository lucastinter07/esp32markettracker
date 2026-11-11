// The price-alert banner: a green (up) or red (down) strip across the top of the Home or
// Detail screen, e.g. "NVDA up 5.24% today, $512.30   TAP TO VIEW".
#pragma once
#include <stdint.h>
#include "alerts.h"   // market_core: AlertDirection

namespace banner {

static const int16_t HEIGHT = 30;

void draw(int stockIndex, AlertDirection dir);
bool isTap(int16_t x, int16_t y);

}  // namespace banner
