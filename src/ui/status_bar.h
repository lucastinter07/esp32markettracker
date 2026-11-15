// The status line at the top of Home and Sectors (Phase 9 design), 28 px tall:
//   ● Open  closes in 2h 14m   [NOTICE]                  Wed Nov 12 · 1:46 PM
// Market phase with a colored dot and countdown on the left, date and time (US Eastern) on
// the right, and an optional notice pill (e.g. PRICES DELAYED) after the countdown.
// Falls back to the Wi-Fi state until the clock is synced.
#pragma once
#include <stdint.h>

constexpr int16_t STATUS_H = 28;

// force = true redraws even if nothing changed (when a screen is first drawn). Otherwise
// it only redraws when its text changes, i.e. about once a minute.
void statusBarDraw(bool force, const char* notice = nullptr, uint16_t noticeColor = 0);
