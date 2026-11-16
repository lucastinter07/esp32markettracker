// The big chart on the Detail screen (Phase 9, layout D1). No text is ever drawn on the
// plot itself: price labels sit in a column to its right (with the previous close "P" and
// price targets "T" as tags there), times / weekdays / dates in a row under it, and a
// legend row under that.
#pragma once
#include <stdint.h>
#include "symbol_state.h"

enum class ChartRange : uint8_t {
  Hour,      // last 60 minutes of today's points
  Day,       // today's session, 9:30 to the close
  FiveDay,   // last 5 sessions: backfilled 30-min closes + today's points
  Month,     // ~21 daily closes + today's price
};

// The plot area (where the line goes). The label column starts 8 px right of it (64 px
// wide), the time row 6 px under it, the legend row 23 px under it; the compare chart's
// legend goes 16 px above it. The caller clears the whole area first.
struct ChartBox {
  int16_t x, y, w, h;
};

// marks: price targets (8C), drawn as dashed lines with a "T" tag when they're inside the
// price range; a target close to the prices widens the range a little to fit.
void drawRangeChart(const SymbolState& s, const ChartBox& box, ChartRange range, const float* marks = nullptr,
                    int markCount = 0);

// The price the range's change is measured from: the price an hour ago (Hour), the
// previous close (Day), the close 5 / 21 sessions back (FiveDay / Month).
// False if there isn't enough data yet.
bool rangeReference(const SymbolState& s, ChartRange range, float& reference);

// Compare vs SPY (8B): the stock and `bench` (SPY) as % change from each one's reference
// price for the range, % labels, legend row above: "NVDA +1.24%  SPY +0.41%  = +0.83% VS SPY".
void drawCompareChart(const SymbolState& s, const SymbolState& bench, const ChartBox& box, ChartRange range);

// The legend's figures: each one's change over the range. False if either lacks a reference.
bool compareFigures(const SymbolState& s, const SymbolState& bench, ChartRange range, float& stockPct,
                    float& benchPct);

// "1H", "1D", "5D", "1M"
const char* rangeName(ChartRange range);
