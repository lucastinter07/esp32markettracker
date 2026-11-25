#include "app/price_alerts.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "data/settings.h"
#include "data/targets.h"
#include "hal/buzzer.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "symbol_text.h"

static AlertState states[cfg::NUM_TICKERS];
static char stateSymbol[cfg::NUM_TICKERS][SYMBOL_MAX_LEN + 1];   // ticker the state belongs to
static uint32_t checkedVersion[cfg::NUM_TICKERS];
static bool pending = false;
static AlertEvent pendingEvent;

// The fired flags are saved in NVS (namespace "alerts") whenever an alert fires, so a
// restart during the day doesn't sound the same alert again. Flags from an earlier day are
// reset by checkAlert(); flags for a ticker that's no longer in its slot are dropped.
static const uint8_t ALERTS_VERSION = 1;
struct SavedAlerts {
  char symbol[cfg::NUM_TICKERS][SYMBOL_MAX_LEN + 1];
  AlertState state[cfg::NUM_TICKERS];
};

static void saveStates() {
  static SavedAlerts saved;   // static: keeps it off the stack
  memcpy(saved.symbol, stateSymbol, sizeof(saved.symbol));
  memcpy(saved.state, states, sizeof(saved.state));
  Preferences prefs;
  prefs.begin("alerts", false);
  prefs.putUChar("version", ALERTS_VERSION);
  prefs.putBytes("data", &saved, sizeof(saved));
  prefs.end();
}

void alertsInit() {
  static SavedAlerts saved;
  Preferences prefs;
  prefs.begin("alerts", false);
  bool ok = prefs.getUChar("version", 0) == ALERTS_VERSION && prefs.getBytesLength("data") == sizeof(saved) &&
            prefs.getBytes("data", &saved, sizeof(saved)) == sizeof(saved);
  prefs.end();
  if (!ok) return;
  int restored = 0;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    saved.symbol[i][SYMBOL_MAX_LEN] = '\0';
    if (strcmp(saved.symbol[i], stock(i).symbol) != 0) continue;   // the slot has another ticker now
    strcpy(stateSymbol[i], saved.symbol[i]);
    states[i] = saved.state[i];
    if (states[i].upFired || states[i].downFired) restored++;
  }
  if (restored) Serial.printf("alerts: restored the fired flags of %d stocks\n", restored);
}

static void raise(const AlertEvent& ev) {
  pending = true;
  pendingEvent = ev;
}

void alertsUpdate() {
  if (cfg::DEMO_PRICES || !marketIsOpenNow()) return;
  EtTime now;
  if (!timeNowET(now)) return;
  int32_t today = dayNumber(now.year, now.month, now.day);
  // Hold everything until ALERT_HOLD_S after the open (see config.h).
  uint32_t nowUnix = (uint32_t)time(nullptr);
  uint32_t open, close;
  sessionBounds(nowUnix, open, close);
  uint32_t holdEnd = open + cfg::ALERT_HOLD_S;
  if (nowUnix < holdEnd) return;

  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    const SymbolState& s = stock(i);
    if (!s.hasData || s.version == checkedVersion[i]) continue;
    checkedVersion[i] = s.version;
    if (s.fetchedAt < holdEnd) continue;   // fetched before the hold ended (e.g. before the open)
    if (strcmp(stateSymbol[i], s.symbol) != 0) {   // a new ticker in this slot starts fresh
      strcpy(stateSymbol[i], s.symbol);
      states[i] = AlertState();
    }

    // Price target (8C)
    bool above;
    float targetPrice;
    if (targetsCheck(i, s.quote.price, today, above, targetPrice)) {
      Serial.printf("alert: %s reached its %s target %.2f (now %.2f)\n", s.symbol, above ? "above" : "below",
                    targetPrice, s.quote.price);
      buzzerTarget();
      raise(AlertEvent{i, above ? AlertDirection::Up : AlertDirection::Down, true, targetPrice});
      continue;   // one alert per quote is enough; the daily move is checked on the next one
    }

    // Daily move (6G)
    if (settings().alertPercent == 0 && cfg::ALERT_TEST_PERCENT <= 0) continue;
    float threshold = cfg::ALERT_TEST_PERCENT > 0 ? cfg::ALERT_TEST_PERCENT : settings().alertPercent;
    AlertDirection dir = checkAlert(states[i], s.quote.changePct, threshold, today);
    if (dir == AlertDirection::None) continue;

    bool up = dir == AlertDirection::Up;
    Serial.printf("alert: %s %s %.2f%% (threshold %.1f%%)\n", s.symbol, up ? "up" : "down",
                  s.quote.changePct, threshold);
    buzzerAlert(up);
    raise(AlertEvent{i, dir, false, 0});
    saveStates();
  }
}

bool alertsTake(AlertEvent& out) {
  if (!pending) return false;
  pending = false;
  out = pendingEvent;
  return true;
}
