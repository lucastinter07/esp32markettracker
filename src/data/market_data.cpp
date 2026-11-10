#include "data/market_data.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/watchlist.h"
#include "symbol_text.h"

static SymbolState stocks[cfg::NUM_TICKERS];
static SymbolState indexes[cfg::NUM_INDEXES];

// Each SymbolState::symbol points into this, so a slot can be renamed at runtime.
static char names[NUM_SYMBOLS][SYMBOL_MAX_LEN + 1];
static volatile uint8_t generation[NUM_SYMBOLS];
static portMUX_TYPE nameLock = portMUX_INITIALIZER_UNLOCKED;

void marketDataInit() {
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    strncpy(names[i], watchlistSymbol(i), SYMBOL_MAX_LEN);
    names[i][SYMBOL_MAX_LEN] = '\0';
    symbolByIndex(i).symbol = names[i];
  }
}

SymbolState& stock(int i) {
  return stocks[i];
}

SymbolState& indexEtf(int i) {
  return indexes[i];
}

bool isStale(const SymbolState& s, uint32_t nowMs, bool marketOpen) {
  if (cfg::DEMO_PRICES || !marketOpen || !s.hasData) return false;
  return nowMs - s.lastUpdateMs > cfg::STALE_AFTER_S * 1000;
}

SymbolState& symbolByIndex(int i) {
  return i < cfg::NUM_TICKERS ? stocks[i] : indexes[i - cfg::NUM_TICKERS];
}

void marketDataReplace(int i, const char* symbol) {
  symbolByIndex(i).clearData();
  taskENTER_CRITICAL(&nameLock);
  strncpy(names[i], symbol, SYMBOL_MAX_LEN);
  names[i][SYMBOL_MAX_LEN] = '\0';
  generation[i]++;
  taskEXIT_CRITICAL(&nameLock);
}

uint8_t symbolSnapshot(int i, char* out) {
  taskENTER_CRITICAL(&nameLock);
  memcpy(out, names[i], SYMBOL_MAX_LEN + 1);
  uint8_t gen = generation[i];
  taskEXIT_CRITICAL(&nameLock);
  return gen;
}

uint8_t symbolGeneration(int i) {
  return generation[i];
}
