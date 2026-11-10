// Live prices from Finnhub.
//
// A background FreeRTOS task (on core 0, next to the Wi-Fi driver) requests one
// /quote at a time, round-robin through all symbols, over a single reused HTTPS
// connection. Each result goes into a queue; quoteClientApply() in the main loop
// moves it into market data. So the slow network work never blocks the screen, and
// market data is only ever changed by the main loop (no locking needed).
//
// While the market is open, each symbol is refreshed about every cfg::QUOTE_REFRESH_S
// seconds. While it's closed, the client fetches every symbol once (at boot, and a
// couple of minutes after the close for final prices) and then stays idle.
#pragma once
#include <stdint.h>
#include "symbol_state.h"
#include "symbol_text.h"

enum class FeedStatus : uint8_t {
  Starting,      // waiting for Wi-Fi / first quote
  Live,          // quotes arriving
  Idle,          // market closed and all prices up to date
  NoKey,         // FINNHUB_API_KEY missing from .env at build time
  BadKey,        // Finnhub rejected the key (HTTP 401/403)
  RateLimited,   // HTTP 429: too many requests, backing off
  NetError,      // connection or response problem; retrying
};

void quoteClientStart();                 // call once in setup()
void quoteClientApply(uint32_t nowMs);   // call every loop
FeedStatus quoteFeedStatus();

// Short label for the Home screen, or nullptr when everything is fine.
const char* quoteFeedProblem();

// A watchlist slot got a new ticker (called by data/watchlist after the data was reset):
// prices it next and downloads its names, stats, earnings and history.
void quoteClientSymbolChanged(int i);

// Checking a ticker typed in the watchlist editor: Finnhub quote + company name.
enum class LookupStatus : uint8_t { Found, NotFound, Failed };
struct SymbolLookup {
  char symbol[SYMBOL_MAX_LEN + 1];
  LookupStatus status;
  Quote quote;
  Profile profile;   // valid = false for ETFs (no profile)
};
void quoteClientLookup(const char* symbol);       // starts a check (replaces any pending one)
bool quoteClientLookupResult(SymbolLookup& out);  // true once an answer is ready
