// Price targets per stock slot (8C): one "above" and one "below", saved in NVS (namespace
// "targets") so they survive restarts, including the one-shot "already hit" mark.
// Targets belong to the ticker they were set for: when a slot gets a new ticker, its
// targets are dropped. Main loop only.
#pragma once
#include <stdint.h>
#include "price_targets.h"   // market_core: PriceTarget, checkTarget

void targetsInit();   // call after marketDataInit()

PriceTarget targetGet(int stockIndex, bool above);
void targetPut(int stockIndex, bool above, float price);   // 0 = remove; re-arms; saves

// A new quote for stock i: fires (and saves) any target it reached. Returns true for the
// first one that fired, with which side and its price.
bool targetsCheck(int stockIndex, float price, int32_t today, bool& above, float& targetPrice);
