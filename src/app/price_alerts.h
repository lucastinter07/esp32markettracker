// Price alerts (6G): when a stock's move today reaches ±(Settings > Price alerts) %, the
// buzzer plays a rising or falling pattern and the screen manager shows a banner.
// Each direction fires at most once per stock per day; only during the regular session.
#pragma once
#include "alerts.h"   // market_core: AlertDirection, checkAlert

void alertsUpdate();   // call every loop: checks stocks whose quote changed
// The newest alert not yet shown, if any (the latest one wins if several arrive at once).
bool alertsTake(int& stockIndex, AlertDirection& dir);
