#include "data/fake_feed.h"
#include <Arduino.h>
#include "config.h"
#include "data/market_data.h"

static const uint32_t UPDATE_INTERVAL_MS = 2000;
static const int TOTAL = NUM_SYMBOLS;

static const float START_PRICES[TOTAL] = {131.50f, 158.20f, 22.85f, 95.40f, 228.00f,  // stocks
                                          582.10f, 501.20f};                          // SPY, QQQ
static float prevClose[TOTAL];
static int nextSymbol = 0;
static uint32_t lastUpdate = 0;

// Moves the price up to +/-0.3% and fills in a quote the way Finnhub would.
static void step(int i, uint32_t nowMs) {
  SymbolState& s = symbolByIndex(i);
  float last = s.hasData ? s.quote.price : START_PRICES[i];
  float price = last * (1.0f + random(-300, 301) / 100000.0f);

  Quote q;
  q.price = price;
  q.prevClose = prevClose[i];
  q.change = price - q.prevClose;
  q.changePct = q.change / q.prevClose * 100.0f;
  q.open = s.hasData ? s.quote.open : price;
  q.high = s.hasData ? max(s.quote.high, price) : price;
  q.low = s.hasData ? min(s.quote.low, price) : price;
  s.applyQuote(q, nowMs);
}

void fakeFeedInit(uint32_t nowMs) {
  randomSeed(esp_random());
  for (int i = 0; i < TOTAL; i++) {
    prevClose[i] = START_PRICES[i] * (1.0f + random(-150, 151) / 10000.0f);
    // Pre-fill some history so charts have something to show right away.
    for (int n = 0; n < 120; n++) step(i, nowMs);
  }
  lastUpdate = nowMs;
}

void fakeFeedUpdate(uint32_t nowMs) {
  if (nowMs - lastUpdate < UPDATE_INTERVAL_MS) return;
  lastUpdate = nowMs;
  step(nextSymbol, nowMs);
  nextSymbol = (nextSymbol + 1) % TOTAL;
}
