// Overnight clock (8D): replaces the Home / Detail / Sectors cycle while the market is
// closed (Settings > Overnight clock). Big time and date, when the market opens next, the
// last session's change for the 2 indexes and 5 stocks, and earnings in the next 7 days.
// Any tap or button goes to Home; the screen manager brings the clock back after 60 s idle.
#pragma once

namespace clockui {

void drawFull();
void update();   // redraws only the parts whose text changed (the time once a minute)

}  // namespace clockui
