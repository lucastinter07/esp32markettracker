// Stand-in data source: random-walk prices, updating one symbol every 2 s in
// round-robin order, the same pattern the real Finnhub scheduler will use.
// Lets the whole UI be built and tested before Wi-Fi exists.
#pragma once
#include <stdint.h>

void fakeFeedInit(uint32_t nowMs);
void fakeFeedUpdate(uint32_t nowMs);
