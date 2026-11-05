// Everything the device knows about one symbol: the latest quote plus its stored history.
#pragma once
#include <stdint.h>
#include "price_series.h"

// Mirrors the fields of Finnhub's /quote response.
struct Quote {
  float price = 0;       // c
  float change = 0;      // d
  float changePct = 0;   // dp
  float high = 0;        // h
  float low = 0;         // l
  float open = 0;        // o
  float prevClose = 0;   // pc
  uint32_t timestamp = 0;  // t (unix seconds)
};

struct SymbolState {
  const char* symbol = "";
  Quote quote;
  PriceSeries intraday;
  bool hasData = false;
  uint32_t lastUpdateMs = 0;
  // Goes up by one on every update. The UI compares it to the value it last drew,
  // so it redraws only symbols that actually changed.
  uint32_t version = 0;

  void applyQuote(const Quote& q, uint32_t nowMs);
};
