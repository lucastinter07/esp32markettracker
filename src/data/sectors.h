// The sector heat map's data: the 11 sector ETFs from cfg::SECTOR_SYMBOLS plus SPY as the
// benchmark (always the last entry, so SPY is tracked even when it isn't an index slot).
// Only price and daily change: no charts, stats or alerts. Written by the main loop only
// (the quote task sends results through its queue), like market_data.
#pragma once
#include <stdint.h>
#include "config.h"
#include "symbol_state.h"

constexpr int NUM_SECTOR_QUOTES = cfg::NUM_SECTORS + 1;
constexpr int BENCHMARK_INDEX = cfg::NUM_SECTORS;   // SPY

// Plain data so it can be saved to flash as raw bytes.
struct SectorQuote {
  bool valid = false;
  float price = 0;
  float change = 0;
  float changePct = 0;
  uint32_t fetchedAt = 0;   // unix seconds
};

const char* sectorSymbol(int i);   // 0 .. NUM_SECTOR_QUOTES-1
const SectorQuote& sectorQuote(int i);
void sectorsApply(int i, const Quote& q, uint32_t fetchedAt);
void sectorsRestore(int i, const SectorQuote& saved);   // from flash at boot
uint32_t sectorsVersion(int i);   // goes up on every change, so the screen redraws only what changed
uint32_t sectorsNewestFetch();    // unix seconds of the newest quote (0 = none yet)
