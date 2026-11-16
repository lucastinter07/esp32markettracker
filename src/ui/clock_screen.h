// Overnight clock (8D, layout K2 since 9D): replaces the Home / Detail / Sectors cycle while
// the market is closed (Settings > Overnight clock). Left: weekday, date, big time, market
// state and a "next open" card with the countdown. Right: the last close of the 2 index
// ETFs and 5 stocks, and earnings coming up this week.
// Any tap or button goes to Home; the screen manager brings the clock back after 60 s idle.
#pragma once

namespace clockui {

void drawFull();
void update();   // redraws only the parts whose text changed (the time once a minute)

}  // namespace clockui
