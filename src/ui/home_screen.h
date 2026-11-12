// Home screen: header, 5 ticker rows with sparklines, index bar.
#pragma once
#include <stdint.h>

namespace home {

void drawFull(int highlightRow);   // whole screen (when switching to Home)
void update(int highlightRow);     // only the rows whose data or highlight changed
int rowAt(int16_t x, int16_t y);   // ticker row under a touch, or -1
bool isMenuTap(int16_t x, int16_t y);   // the menu icon at the top left (opens Settings)
bool isIndexBarTap(int16_t x, int16_t y);   // the index bar at the bottom (opens the sector map)

}  // namespace home
