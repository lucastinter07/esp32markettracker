// The big labelled chart on the detail screen: frame, gridlines, price labels on the
// right, time/day/date labels underneath, high/low markers and a last-price tag.
// (The small Home-screen sparklines use ui/chart.)
#pragma once
#include <stdint.h>
#include "symbol_state.h"

enum class ChartRange : uint8_t {
  Hour,      // last 60 minutes of today's points
  Day,       // today's session, 9:30 to the close
  FiveDay,   // last 5 sessions: backfilled 30-min closes + today's points
  Month,     // ~21 daily closes + today's price
};

// The whole chart region, including the label column on the right and the time row
// underneath. The plotted area itself sits inside it.
struct ChartBox {
  int16_t x, y, w, h;
};

void drawRangeChart(const SymbolState& s, const ChartBox& box, ChartRange range);

// The price the range's change is measured from: the price an hour ago (Hour), the
// previous close (Day), the close 5 / 21 sessions back (FiveDay / Month).
// False if there isn't enough data yet.
bool rangeReference(const SymbolState& s, ChartRange range, float& reference);

// Compare vs SPY (8B): the stock and `bench` (SPY) as % change from each one's reference
// price for the range, Y axis in %, with a legend "NVDA +2.10%  SPY +0.90%  +1.20% vs SPY".
void drawCompareChart(const SymbolState& s, const SymbolState& bench, const ChartBox& box, ChartRange range);

// The legend's figures: each one's change over the range. False if either lacks a reference.
bool compareFigures(const SymbolState& s, const SymbolState& bench, ChartRange range, float& stockPct,
                    float& benchPct);

// "1H", "1D", "5D", "1M"
const char* rangeName(ChartRange range);
