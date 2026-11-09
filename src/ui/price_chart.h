// The big labelled chart on the detail screen: frame, gridlines, price labels on the
// right, time labels underneath, a dashed previous-close line, day high/low markers
// and a tag showing the last price. (The small Home-screen sparklines use ui/chart.)
#pragma once
#include <stdint.h>
#include "symbol_state.h"

// The whole chart region, including the label column on the right and the time row
// underneath. The plotted area itself sits inside it.
struct ChartBox {
  int16_t x, y, w, h;
};

// Draws today's (or the most recent session's) chart for one symbol.
void drawDayChart(const SymbolState& s, const ChartBox& box);
