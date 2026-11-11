#include "app/price_alerts.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "data/settings.h"
#include "hal/buzzer.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "symbol_text.h"

static AlertState states[cfg::NUM_TICKERS];
static char stateSymbol[cfg::NUM_TICKERS][SYMBOL_MAX_LEN + 1];   // ticker the state belongs to
static uint32_t checkedVersion[cfg::NUM_TICKERS];
static bool pending = false;
static int pendingIndex = 0;
static AlertDirection pendingDir = AlertDirection::None;

void alertsUpdate() {
  if (cfg::DEMO_PRICES || settings().alertPercent == 0 || !marketIsOpenNow()) return;
  EtTime now;
  if (!timeNowET(now)) return;
  int32_t today = dayNumber(now.year, now.month, now.day);

  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    const SymbolState& s = stock(i);
    if (!s.hasData || s.version == checkedVersion[i]) continue;
    checkedVersion[i] = s.version;
    if (strcmp(stateSymbol[i], s.symbol) != 0) {   // a new ticker in this slot starts fresh
      strcpy(stateSymbol[i], s.symbol);
      states[i] = AlertState();
    }
    float threshold = cfg::ALERT_TEST_PERCENT > 0 ? cfg::ALERT_TEST_PERCENT : settings().alertPercent;
    AlertDirection dir = checkAlert(states[i], s.quote.changePct, threshold, today);
    if (dir == AlertDirection::None) continue;

    bool up = dir == AlertDirection::Up;
    Serial.printf("alert: %s %s %.2f%% (threshold %.1f%%)\n", s.symbol, up ? "up" : "down",
                  s.quote.changePct, threshold);
    buzzerAlert(up);
    pending = true;
    pendingIndex = i;
    pendingDir = dir;
  }
}

bool alertsTake(int& stockIndex, AlertDirection& dir) {
  if (!pending) return false;
  pending = false;
  stockIndex = pendingIndex;
  dir = pendingDir;
  return true;
}
