#include "data/watchlist.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "net/quote_client.h"
#include "symbol_text.h"

// Saved as two blocks of bytes. Bump the version if the layout changes, so an old block
// is ignored instead of being misread.
static const uint8_t WATCHLIST_VERSION = 1;

static char stocks[cfg::NUM_TICKERS][SYMBOL_MAX_LEN + 1];
static uint8_t indexChoices[cfg::NUM_INDEXES];

static void loadDefaults() {
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    strncpy(stocks[i], cfg::TICKERS[i], SYMBOL_MAX_LEN);
    stocks[i][SYMBOL_MAX_LEN] = '\0';
  }
  for (int k = 0; k < cfg::NUM_INDEXES; k++) indexChoices[k] = cfg::DEFAULT_INDEX_CHOICES[k];
}

static void save() {
  Preferences prefs;
  prefs.begin("watchlist", false);
  prefs.putUChar("version", WATCHLIST_VERSION);
  prefs.putBytes("stocks", stocks, sizeof(stocks));
  prefs.putBytes("indexes", indexChoices, sizeof(indexChoices));
  prefs.end();
}

void watchlistInit() {
  loadDefaults();
  Preferences prefs;
  prefs.begin("watchlist", false);
  bool ok = false;
  if (prefs.getUChar("version", 0) == WATCHLIST_VERSION) {
    char savedStocks[cfg::NUM_TICKERS][SYMBOL_MAX_LEN + 1];
    uint8_t savedIndexes[cfg::NUM_INDEXES];
    ok = prefs.getBytes("stocks", savedStocks, sizeof(savedStocks)) == sizeof(savedStocks) &&
         prefs.getBytes("indexes", savedIndexes, sizeof(savedIndexes)) == sizeof(savedIndexes);
    // Only accept it if every entry still makes sense.
    for (int i = 0; ok && i < cfg::NUM_TICKERS; i++) {
      savedStocks[i][SYMBOL_MAX_LEN] = '\0';
      ok = isValidSymbol(savedStocks[i]);
    }
    for (int k = 0; ok && k < cfg::NUM_INDEXES; k++) ok = savedIndexes[k] < cfg::NUM_INDEX_CHOICES;
    if (ok) {
      memcpy(stocks, savedStocks, sizeof(stocks));
      memcpy(indexChoices, savedIndexes, sizeof(indexChoices));
    }
  }
  prefs.end();
  Serial.printf("watchlist: %s:", ok ? "saved" : "defaults");
  for (int i = 0; i < NUM_SYMBOLS; i++) Serial.printf(" %s", watchlistSymbol(i));
  Serial.println();
}

const char* watchlistStock(int i) {
  return stocks[i];
}

uint8_t watchlistIndexChoice(int k) {
  return indexChoices[k];
}

const char* watchlistIndexLabel(int k) {
  return cfg::INDEX_CHOICE_LABELS[indexChoices[k]];
}

const char* watchlistSymbol(int i) {
  if (i < cfg::NUM_TICKERS) return stocks[i];
  return cfg::INDEX_CHOICE_SYMBOLS[indexChoices[i - cfg::NUM_TICKERS]];
}

int watchlistFind(const char* symbol) {
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    if (strcmp(watchlistSymbol(i), symbol) == 0) return i;
  }
  return -1;
}

// The slot's symbol changed: start it over with no data and fetch everything for it.
static void symbolChanged(int i) {
  marketDataReplace(i, watchlistSymbol(i));
  quoteClientSymbolChanged(i);
  Serial.printf("watchlist: slot %d is now %s\n", i, watchlistSymbol(i));
}

void watchlistSetStock(int i, const char* symbol) {
  if (strcmp(stocks[i], symbol) == 0) return;
  strncpy(stocks[i], symbol, SYMBOL_MAX_LEN);
  stocks[i][SYMBOL_MAX_LEN] = '\0';
  save();
  symbolChanged(i);
}

void watchlistSetIndexChoice(int k, uint8_t choice) {
  if (indexChoices[k] == choice) return;
  indexChoices[k] = choice;
  save();
  symbolChanged(cfg::NUM_TICKERS + k);
}

void watchlistReset() {
  char before[NUM_SYMBOLS][SYMBOL_MAX_LEN + 1];
  for (int i = 0; i < NUM_SYMBOLS; i++) strcpy(before[i], watchlistSymbol(i));
  loadDefaults();
  Preferences prefs;
  prefs.begin("watchlist", false);
  prefs.clear();   // nothing saved = defaults at the next boot too
  prefs.end();
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    if (strcmp(before[i], watchlistSymbol(i)) != 0) symbolChanged(i);
  }
}

bool watchlistIsDefault() {
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    if (strcmp(stocks[i], cfg::TICKERS[i]) != 0) return false;
  }
  for (int k = 0; k < cfg::NUM_INDEXES; k++) {
    if (indexChoices[k] != cfg::DEFAULT_INDEX_CHOICES[k]) return false;
  }
  return true;
}
