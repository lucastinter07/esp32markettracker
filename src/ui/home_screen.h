// Home screen (Phase 9, layout A): header bar (menu, market phase, date and time), column
// headings, the 5 stocks as rows (symbol, price, change, %, today's chart) and the 2 index
// ETFs along the bottom.
#pragma once
#include <stdint.h>

namespace home {

void drawFull(int highlightRow);   // whole screen (when switching to Home)
void update(int highlightRow);     // only the parts whose data or highlight changed
int rowAt(int16_t x, int16_t y);   // stock row under a touch, or -1
bool isMenuTap(int16_t x, int16_t y);       // the menu icon at the top left (opens Settings)
bool isIndexBarTap(int16_t x, int16_t y);   // the index line at the bottom (opens the sector map)

}  // namespace home
