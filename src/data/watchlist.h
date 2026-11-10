// Which symbols the device tracks: 5 stock slots and 2 index slots. Saved in NVS, so a
// ticker changed on the device survives restarts; config.h only gives the defaults.
// Changing a slot also resets that symbol's market data and asks the quote client to
// download everything for the new one.
#pragma once
#include <stdint.h>

void watchlistInit();   // call before marketDataInit()

const char* watchlistStock(int i);        // 0 .. cfg::NUM_TICKERS-1
uint8_t watchlistIndexChoice(int k);      // 0 .. cfg::NUM_INDEXES-1 -> cfg::INDEX_CHOICE_*
const char* watchlistIndexLabel(int k);   // "S&P 500"
const char* watchlistSymbol(int i);       // 0 .. NUM_SYMBOLS-1, stocks then index ETFs

// -1 if `symbol` isn't in any slot, else its symbol number (stocks then indexes).
int watchlistFind(const char* symbol);

void watchlistSetStock(int i, const char* symbol);
void watchlistSetIndexChoice(int k, uint8_t choice);
void watchlistReset();                    // back to the config.h defaults
bool watchlistIsDefault();
