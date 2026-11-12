#include "data/sectors.h"

static SectorQuote quotes[NUM_SECTOR_QUOTES];
static uint32_t versions[NUM_SECTOR_QUOTES];

const char* sectorSymbol(int i) {
  return i == BENCHMARK_INDEX ? cfg::BENCHMARK_SYMBOL : cfg::SECTOR_SYMBOLS[i];
}

const SectorQuote& sectorQuote(int i) {
  return quotes[i];
}

void sectorsApply(int i, const Quote& q, uint32_t fetchedAt) {
  SectorQuote& s = quotes[i];
  s.valid = true;
  s.price = q.price;
  s.change = q.change;
  s.changePct = q.changePct;
  s.fetchedAt = fetchedAt;
  versions[i]++;
}

void sectorsRestore(int i, const SectorQuote& saved) {
  if (!saved.valid || saved.fetchedAt <= quotes[i].fetchedAt) return;   // a live quote already came in
  quotes[i] = saved;
  versions[i]++;
}

uint32_t sectorsVersion(int i) {
  return versions[i];
}

uint32_t sectorsNewestFetch() {
  uint32_t newest = 0;
  for (const SectorQuote& s : quotes) {
    if (s.valid && s.fetchedAt > newest) newest = s.fetchedAt;
  }
  return newest;
}
