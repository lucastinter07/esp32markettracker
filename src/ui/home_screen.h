// Home screen (Phase 9 design): status line, the 2 index ETFs as chips, the 5 stocks as
// cards (symbol + name, intraday sparkline, price, change pill), and the bottom tab bar.
#pragma once
#include <stdint.h>

namespace home {

void drawFull(int highlightRow);   // whole screen (when switching to Home)
void update(int highlightRow);     // only the parts whose data or highlight changed
int rowAt(int16_t x, int16_t y);   // stock card under a touch, or -1
bool isIndexTap(int16_t x, int16_t y);   // the index chips (open the sector map)

}  // namespace home
