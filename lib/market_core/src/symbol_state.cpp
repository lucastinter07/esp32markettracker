#include "symbol_state.h"

void SymbolState::applyQuote(const Quote& q, uint32_t nowMs, bool addToHistory) {
  quote = q;
  if (addToHistory) intraday.push(q.price);
  // Keep the 52-week range current between weekly fetches.
  if (fundamentals.valid) {
    if (q.high > fundamentals.high52) fundamentals.high52 = q.high;
    if (q.low > 0 && q.low < fundamentals.low52) fundamentals.low52 = q.low;
  }
  hasData = true;
  lastUpdateMs = nowMs;
  version++;
}

void SymbolState::applyMetrics(const MetricData& m, int32_t today) {
  fundamentals.valid = true;
  fundamentals.high52 = m.high52;
  fundamentals.low52 = m.low52;
  fundamentals.epsTTM = m.epsTTM;
  fundamentals.sharesM = (hasData && quote.price > 0) ? m.marketCapM / quote.price : 0;
  fundamentals.fetchedDay = today;
  version++;
}

float SymbolState::peRatio() const {
  if (!fundamentals.valid || !hasData || fundamentals.epsTTM <= 0) return 0;
  return quote.price / fundamentals.epsTTM;
}

double SymbolState::marketCap() const {
  if (!fundamentals.valid || !hasData) return 0;
  return (double)fundamentals.sharesM * 1e6 * quote.price;
}
