#include "symbol_state.h"

void SymbolState::applyQuote(const Quote& q, uint32_t nowMs) {
  quote = q;
  intraday.push(q.price);
  hasData = true;
  lastUpdateMs = nowMs;
  version++;
}
