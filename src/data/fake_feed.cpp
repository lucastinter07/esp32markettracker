#include "data/fake_feed.h"
#include <Arduino.h>
#include "config.h"
#include "data/market_data.h"
#include "data/sectors.h"

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
  if (i >= cfg::NUM_TICKERS && strcmp(s.symbol, cfg::BENCHMARK_SYMBOL) == 0) benchmark().applyQuote(q, nowMs);
}

// Sector map: a random daily change per sector ETF, nudged a little on each update.
static void stepSector(int k) {
  const SectorQuote& old = sectorQuote(k);
  Quote q;
  q.changePct = old.valid ? old.changePct + random(-20, 21) / 100.0f : random(-250, 251) / 100.0f;
  q.price = 100.0f * (1.0f + q.changePct / 100.0f);
  q.change = q.price - 100.0f;
  sectorsApply(k, q, (uint32_t)time(nullptr));
}

void fakeFeedInit(uint32_t nowMs) {
  randomSeed(esp_random());
  for (int i = 0; i < TOTAL; i++) {
    prevClose[i] = START_PRICES[i] * (1.0f + random(-150, 151) / 10000.0f);
    // Pre-fill some history so charts have something to show right away.
    for (int n = 0; n < 120; n++) step(i, nowMs);
  }
  for (int k = 0; k < NUM_SECTOR_QUOTES; k++) stepSector(k);
  lastUpdate = nowMs;
}

void fakeFeedUpdate(uint32_t nowMs) {
  if (nowMs - lastUpdate < UPDATE_INTERVAL_MS) return;
  lastUpdate = nowMs;
  step(nextSymbol, nowMs);
  stepSector(random(0, NUM_SECTOR_QUOTES));
  nextSymbol = (nextSymbol + 1) % TOTAL;
}
