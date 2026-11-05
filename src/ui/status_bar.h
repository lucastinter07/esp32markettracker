// The Home screen's top bar: market phase and countdown on the left, date and time
// (US Eastern) on the right. Falls back to Wi-Fi status until the clock is synced.
#pragma once

// force = true redraws even if nothing changed (when the Home screen is first drawn).
// Otherwise it only redraws when the text changes, i.e. about once a minute.
void statusBarDraw(bool force);
