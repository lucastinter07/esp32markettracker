// The Settings screen: a list of rows (label on the left, value on the right). Tapping a
// row, or SELECT on the highlighted row, steps its value to the next option. Every change
// is saved straight away.
#pragma once
#include <stdint.h>

namespace settingsui {

enum class Action : uint8_t { None, StartWifiSetup, OpenWatchlist };

int rowCount();
void drawFull(int highlight);
int watchlistRow();                      // the row that opens the watchlist editor
void update(int highlight);              // redraws only rows whose value or highlight changed
Action activate(int row, uint32_t nowMs);   // step the row's value (or confirm an action)
int rowAt(int16_t x, int16_t y);         // row under a tap, or -1
bool isBackTap(int16_t x, int16_t y);

}  // namespace settingsui
