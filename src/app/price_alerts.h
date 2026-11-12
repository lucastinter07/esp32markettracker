// Price alerts, checked on every new quote during the regular session:
// - Daily move (6G): a stock's move today reaches ±(Settings > Price alerts) %. Each
//   direction fires at most once per stock per day. Rising / falling buzzer notes.
// - Price target (8C): a stock reaches a target set in the watchlist editor. One-shot
//   (see data/targets). Its own buzzer pattern. Works even with Settings > Price alerts OFF.
// The screen manager shows a banner for the newest alert.
#pragma once
#include "alerts.h"   // market_core: AlertDirection, checkAlert

struct AlertEvent {
  int stockIndex;
  AlertDirection dir;   // which way the price went (for a target: Up = above target)
  bool target;          // true = price target, false = daily move
  float targetPrice;    // for a target
};

void alertsUpdate();   // call every loop: checks stocks whose quote changed
// The newest alert not yet shown, if any (the latest one wins if several arrive at once).
bool alertsTake(AlertEvent& out);
