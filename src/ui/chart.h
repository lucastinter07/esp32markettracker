// Line chart drawn from a PriceSeries. Used for the small Home sparklines and the
// large Detail chart.
#pragma once
#include <stdint.h>
#include "price_series.h"

// Clears the box to `bg`, then draws the series scaled to fit.
// If drawRef is true, a dashed line marks refValue (e.g. previous close).
void drawLineChart(int16_t x, int16_t y, int16_t w, int16_t h, const PriceSeries& series, uint16_t color, uint16_t bg, bool drawRef = false, float refValue = 0);
