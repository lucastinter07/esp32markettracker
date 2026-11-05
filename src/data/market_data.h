// The single place market data lives. The data source (the fake feed for now,
// the Finnhub client later) writes here, and the UI reads from here.
#pragma once
#include "symbol_state.h"

void marketDataInit();
SymbolState& stock(int i);      // 0 .. cfg::NUM_TICKERS-1
SymbolState& indexEtf(int i);   // 0 .. cfg::NUM_INDEXES-1

// NOTE: everything currently runs in loop(), so no locking is needed. When the
// network moves to its own FreeRTOS task, add a mutex here (see doc 07 §5).
