// The Settings screen: a list of rows (label on the left, value on the right) over two
// pages. Tapping a row, or SELECT on the highlighted row, steps its value to the next
// option; every change is saved straight away. A few rows open another screen or do
// something (and ask for a second tap first).
#pragma once
#include <stdint.h>

namespace settingsui {

enum class Action : uint8_t { None, StartWifiSetup, OpenWatchlist, OpenDiagnostics, RefreshData, Restart };

int rowCount();
int watchlistRow();                      // rows that other screens return to
int diagnosticsRow();
void drawFull(int highlight);            // shows the page the highlighted row is on
void update(int highlight);              // redraws only what changed (or the page, if it changed)
Action activate(int row, uint32_t nowMs);   // step the row's value (or confirm an action)
int rowAt(int16_t x, int16_t y);         // row under a tap on the current page, or -1
int pageTap(int16_t x, int16_t y);       // tap on the page buttons: -1 previous, +1 next, 0 none
int firstRowOfPage(int page);
int currentPage();
int pageCount();
bool isBackTap(int16_t x, int16_t y);

}  // namespace settingsui
