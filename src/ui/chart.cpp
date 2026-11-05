#include "ui/chart.h"
#include "hal/display.h"
#include "ui/theme.h"

void drawLineChart(int16_t x, int16_t y, int16_t w, int16_t h, const PriceSeries& series, uint16_t color, uint16_t bg, bool drawRef, float refValue) {
  DFRobot_GDL& gfx = display();
  gfx.fillRect(x, y, w, h, bg);
  if (series.size() < 2) return;

  float lo = series.minValue();
  float hi = series.maxValue();
  if (drawRef) {
    if (refValue < lo) lo = refValue;
    if (refValue > hi) hi = refValue;
  }
  if (hi - lo < 0.0001f) hi = lo + 0.0001f;   // flat line: avoid divide by zero

  auto toY = [&](float v) -> int16_t {
    return y + (h - 1) - (int16_t)((v - lo) / (hi - lo) * (h - 1));
  };

  if (drawRef) {
    int16_t ry = toY(refValue);
    for (int16_t dx = 0; dx < w; dx += 6) gfx.drawFastHLine(x + dx, ry, 3, theme::DIM);
  }

  size_t n = series.size();
  int16_t prevX = x;
  int16_t prevY = toY(series.at(0));
  for (size_t i = 1; i < n; i++) {
    int16_t px = x + (int16_t)((int32_t)i * (w - 1) / (int32_t)(n - 1));
    int16_t py = toY(series.at(i));
    gfx.drawLine(prevX, prevY, px, py, color);
    prevX = px;
    prevY = py;
  }
}
