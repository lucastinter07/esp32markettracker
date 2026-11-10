// Detail screen for one stock. The top (header, company name, price) is shared by five
// pages, chosen with the tab bar along the bottom:
//   1H / 1D / 5D / 1M: the labelled chart over that range (the change under the price
//                      follows the range too)
//   STATS: today's numbers and company stats in large text
#pragma once
#include <stdint.h>

namespace detail {

// In tab order. SELECT steps through them; the auto cycle shows Day, then Stats.
enum class Page : uint8_t { Hour, Day, FiveDay, Month, Stats };
constexpr int PAGE_COUNT = 5;

void drawFull(int ticker, bool pinned, Page page);
void setPage(int ticker, Page page);     // switch page without redrawing the header
void update(int ticker);                 // redraws data only if it changed
void drawCountdown(float remaining);     // auto mode: bar from 1.0 (full) to 0.0
void setPinned(bool pinned);             // swaps the AUTO / PINNED badge
bool isBackTap(int16_t x, int16_t y);
int tabAt(int16_t x, int16_t y);         // the Page a tap hit in the tab bar, or -1

}  // namespace detail
