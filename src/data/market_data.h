// The single place market data lives. The data source (the Finnhub client, or the
// fake feed in demo mode) writes here, and the UI reads from here.
#pragma once
#include "symbol_state.h"
#include "config.h"

// All tracked symbols: the stocks first, then the index ETFs.
constexpr int NUM_SYMBOLS = cfg::NUM_TICKERS + cfg::NUM_INDEXES;

void marketDataInit();
SymbolState& stock(int i);      // 0 .. cfg::NUM_TICKERS-1
SymbolState& indexEtf(int i);   // 0 .. cfg::NUM_INDEXES-1
SymbolState& symbolByIndex(int i);   // 0 .. NUM_SYMBOLS-1, stocks then index ETFs

// True when the market is open and this symbol hasn't updated for cfg::STALE_AFTER_S.
// marketOpen is passed in so a screen can check it once and reuse it for every row.
bool isStale(const SymbolState& s, uint32_t nowMs, bool marketOpen);

// Only the main loop changes market data. The Finnhub client runs in its own task but
// sends its results through a queue (net/quote_client), so no locking is needed.
