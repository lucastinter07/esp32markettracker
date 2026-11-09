// Everything the device knows about one symbol: the latest quote plus its stored history.
#pragma once
#include <stdint.h>
#include "history.h"
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

// The few fields used from Finnhub's /stock/metric response.
struct MetricData {
  float high52 = 0;       // 52WeekHigh
  float low52 = 0;        // 52WeekLow
  float epsTTM = 0;       // epsTTM: earnings per share over the last 12 months
  float marketCapM = 0;   // marketCapitalization, in millions of dollars
};

// Slow-changing company figures, fetched about weekly and kept current in between:
// the 52-week range is widened by each day's high/low, and P/E and market cap are
// recalculated from the live price. Plain data, so it can be saved to flash as raw bytes.
struct Fundamentals {
  bool valid = false;
  float high52 = 0;
  float low52 = 0;
  float epsTTM = 0;
  float sharesM = 0;        // shares outstanding, in millions (market cap / price at fetch)
  int32_t fetchedDay = 0;   // day number of the fetch (see dayNumber() in market_calendar)
};

// Company details from Finnhub's /stock/profile2, fetched about monthly.
// Fixed-size text arrays (not pointers), so it can be saved to flash as raw bytes.
struct Profile {
  bool valid = false;
  char name[48] = {};       // "Advanced Micro Devices, Inc."
  char industry[32] = {};   // "Semiconductors"
  int32_t fetchedDay = 0;
};

struct SymbolState {
  const char* symbol = "";
  Quote quote;
  PriceSeries intraday;
  Fundamentals fundamentals;
  Profile profile;
  History history;      // backfilled daily + 30-minute closes (Twelve Data)
  bool hasData = false;
  uint32_t lastUpdateMs = 0;
  // Goes up by one on every update. The UI compares it to the value it last drew,
  // so it redraws only symbols that actually changed.
  uint32_t version = 0;

  // addToHistory = false updates the numbers without adding a chart point
  // (used for quotes fetched while the market is closed).
  void applyQuote(const Quote& q, uint32_t nowMs, bool addToHistory = true);

  // Stores freshly fetched metrics. Uses the current price to work out the share count.
  void applyMetrics(const MetricData& m, int32_t today);

  void applyProfile(const Profile& p, int32_t today);

  float peRatio() const;      // price / EPS; 0 when not available or earnings are negative
  double marketCap() const;   // dollars; 0 when not available
};
