#include "data/market_data.h"
#include "config.h"

static SymbolState stocks[cfg::NUM_TICKERS];
static SymbolState indexes[cfg::NUM_INDEXES];

void marketDataInit() {
  for (int i = 0; i < cfg::NUM_TICKERS; i++) stocks[i].symbol = cfg::TICKERS[i];
  for (int i = 0; i < cfg::NUM_INDEXES; i++) indexes[i].symbol = cfg::INDEX_SYMBOLS[i];
}

SymbolState& stock(int i) {
  return stocks[i];
}

SymbolState& indexEtf(int i) {
  return indexes[i];
}
