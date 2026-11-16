// The header bar on Home (Phase 9, layout A), 28 px, in the theme's bar color:
//   ≡ OPEN · 2H 14M                                   THU 11/12 1:46P
// The menu icon (opens Settings), the market phase and its countdown on the left; date and
// time (US Eastern) on the right. Shows the Wi-Fi / clock state until the clock is synced.
#pragma once
#include <stdint.h>

constexpr int16_t STATUS_H = 28;   // = ui::BAR_H

// force = true redraws even if nothing changed (when a screen is first drawn). Otherwise
// it only redraws when its text changes, i.e. about once a minute.
void statusBarDraw(bool force);
