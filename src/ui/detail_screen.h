// Detail screen for one ticker: big price, large chart, stats.
#pragma once
#include <stdint.h>

namespace detail {

void drawFull(int ticker, bool pinned);
void update(int ticker);                 // redraws data only if it changed
void drawCountdown(float remaining);     // auto mode: bar from 1.0 (full) to 0.0
void setPinned(bool pinned);             // swaps the AUTO / PINNED badge
bool isBackTap(int16_t x, int16_t y);

}  // namespace detail
