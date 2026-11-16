// Detail screen for one stock (Phase 9, layout D1 with D2's stats tiles). Header bar
// (< BACK, symbol, company name, PINNED / AUTO), the price row, then one of five pages,
// picked with the buttons along the bottom:
//   [ 1H | 1D | 5D | 1M ]  [ STATS ]  [ □ VS SPY ]
//   1H / 1D / 5D / 1M: the chart over that range (the change in the price row follows it)
//   STATS: 8 tiles (today + company figures) and the next earnings date
// VS SPY is a toggle: the chart pages then show the stock and SPY in %.
#pragma once
#include <stdint.h>

namespace detail {

// In button order. SELECT steps through them; the auto cycle shows Day, then Stats.
enum class Page : uint8_t { Hour, Day, FiveDay, Month, Stats };
constexpr int PAGE_COUNT = 5;

void drawFull(int ticker, bool pinned, Page page);
void setPage(int ticker, Page page);     // switch page without redrawing the header
void update(int ticker);                 // redraws data only if it changed
void drawCountdown(float remaining);     // auto mode: bar from 1.0 (full) to 0.0
void setPinned(bool pinned);             // swaps the AUTO / PINNED badge
bool isBackTap(int16_t x, int16_t y);
int tabAt(int16_t x, int16_t y);         // the Page a tap on the buttons picked, or -1
bool isCompareTap(int16_t x, int16_t y); // the VS SPY button

// Compare vs SPY (8B): the chart pages show the stock and SPY as % change. Stays on across
// pages and stocks; the screen manager turns it off when leaving the Detail screen.
void setCompare(int ticker, bool on);    // redraws what it changes
void resetCompare();                     // off, without drawing (the screen is being left)
bool compareOn();
bool isChartTap(int16_t x, int16_t y);   // a tap on the chart (also toggles compare)

}  // namespace detail
