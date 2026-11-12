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

// SPY (cfg::BENCHMARK_SYMBOL), always tracked for the compare-vs-SPY charts (8B), whether
// or not it's one of the index slots. Not part of symbolByIndex(). Its chart points come from
// the sector passes (every 5 min) and, when SPY is an index slot, from that slot's quotes;
// its history from Twelve Data like a stock's.
SymbolState& benchmark();

// True when the market is open and this symbol hasn't updated for cfg::STALE_AFTER_S.
// marketOpen is passed in so a screen can check it once and reuse it for every row.
bool isStale(const SymbolState& s, uint32_t nowMs, bool marketOpen);

// Changing a watchlist slot (main loop only): clears that symbol's data and gives it the
// new name. Bumps the slot's generation, so results the quote task fetched for the old
// symbol can be recognised and dropped.
void marketDataReplace(int i, const char* symbol);

// For the quote task (other core): copies symbol i's name into out (SYMBOL_MAX_LEN + 1
// bytes) and returns its generation, both read together under a lock so a rename in the
// middle can't produce a half-old, half-new name.
uint8_t symbolSnapshot(int i, char* out);
uint8_t symbolGeneration(int i);

// Only the main loop changes market data. The Finnhub client runs in its own task but
// sends its results through a queue (net/quote_client), so no locking is needed.
