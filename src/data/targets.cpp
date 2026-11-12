#include "data/targets.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"

// Saved as one block of bytes. Bump the version if TargetRecord changes.
static const uint8_t TARGETS_VERSION = 1;

struct TargetRecord {
  char symbol[8];   // the ticker these targets were set for
  PriceTarget above;
  PriceTarget below;
};

static TargetRecord records[cfg::NUM_TICKERS];
static Preferences prefs;

static void save() {
  prefs.begin("targets", false);
  prefs.putUChar("version", TARGETS_VERSION);
  prefs.putBytes("data", records, sizeof(records));
  prefs.end();
}

// The slot's record, emptied first if the slot now holds a different ticker.
static TargetRecord& record(int i) {
  TargetRecord& r = records[i];
  const char* symbol = stock(i).symbol;
  if (strncmp(r.symbol, symbol, sizeof(r.symbol)) != 0) {
    memset(&r, 0, sizeof(r));
    strncpy(r.symbol, symbol, sizeof(r.symbol) - 1);
  }
  return r;
}

void targetsInit() {
  memset(records, 0, sizeof(records));
  prefs.begin("targets", true);
  bool ok = prefs.getUChar("version", 0) == TARGETS_VERSION && prefs.getBytesLength("data") == sizeof(records) &&
            prefs.getBytes("data", records, sizeof(records)) == sizeof(records);
  prefs.end();
  if (!ok) memset(records, 0, sizeof(records));
  int armed = 0;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    const TargetRecord& r = record(i);
    armed += targetArmed(r.above) + targetArmed(r.below);
  }
  Serial.printf("targets: %d armed\n", armed);
}

PriceTarget targetGet(int stockIndex, bool above) {
  const TargetRecord& r = record(stockIndex);
  return above ? r.above : r.below;
}

void targetPut(int stockIndex, bool above, float price) {
  TargetRecord& r = record(stockIndex);
  PriceTarget t = {price > 0 ? price : 0, 0};
  if (above) r.above = t;
  else r.below = t;
  save();
  Serial.printf("targets: %s %s %.2f\n", r.symbol, above ? "above" : "below", t.price);
}

bool targetsCheck(int stockIndex, float price, int32_t today, bool& above, float& targetPrice) {
  TargetRecord& r = record(stockIndex);
  bool fired = false;
  if (checkTarget(r.above, true, price, today)) {
    fired = true;
    above = true;
    targetPrice = r.above.price;
  }
  if (checkTarget(r.below, false, price, today) && !fired) {
    fired = true;
    above = false;
    targetPrice = r.below.price;
  }
  if (fired) save();
  return fired;
}
