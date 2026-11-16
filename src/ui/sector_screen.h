// Sector heat map (Phase 8A, restyled in 9D): the 11 S&P 500 sector ETFs colored by
// today's change, under a header bar "< SECTORS . 6 up 5 down ... SPY +0.41%".
// Two layouts (Settings > Sector map):
//  SIZED: tiles sized by each sector's S&P 500 weight, in fixed places.
//  GRID:  4 x 3 equal tiles sorted by today's change (best top-left), SPY as the last tile.
#pragma once
#include <stdint.h>

namespace sectorsui {

void drawFull();   // whole screen (when switching to it)
void update();     // only the tiles, header and footer text that changed
bool isBackTap(int16_t x, int16_t y);   // "< SECTORS" in the header bar

}  // namespace sectorsui
