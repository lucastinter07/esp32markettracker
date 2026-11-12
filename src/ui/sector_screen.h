// Sector heat map (Phase 8A): the 11 S&P 500 sector ETFs colored by today's change.
// Two layouts (Settings > Sector map):
//  SIZED: tiles sized by each sector's S&P 500 weight, in fixed places; SPY in the header.
//  GRID:  4 x 3 equal tiles sorted by today's change (best top-left), SPY as the last tile.
// The status bar on top is the same as on Home (its menu icon opens Settings).
#pragma once

namespace sectorsui {

void drawFull();   // whole screen (when switching to it)
void update();     // only the tiles, header and footer text that changed

}  // namespace sectorsui
