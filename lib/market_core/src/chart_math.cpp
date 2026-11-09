#include "chart_math.h"
#include <math.h>

// Rounds a rough step up to 1, 2, 2.5, 5 or 10 times a power of ten.
static float niceStep(float rough) {
  float magnitude = powf(10.0f, floorf(log10f(rough)));
  float normalized = rough / magnitude;   // now between 1 and 10
  float nice;
  if (normalized <= 1.0f) nice = 1.0f;
  else if (normalized <= 2.0f) nice = 2.0f;
  else if (normalized <= 2.5f) nice = 2.5f;
  else if (normalized <= 5.0f) nice = 5.0f;
  else nice = 10.0f;
  return nice * magnitude;
}

AxisTicks niceAxis(float minValue, float maxValue, int maxTicks) {
  if (maxTicks < 2) maxTicks = 2;
  float range = maxValue - minValue;
  // A flat line still needs a visible range: use 1% of the price (or 1 if the price is 0).
  if (range <= 0) range = fabsf(maxValue) * 0.01f > 0 ? fabsf(maxValue) * 0.01f : 1.0f;

  float step = niceStep(range / (maxTicks - 1));
  AxisTicks t;
  for (;;) {
    t.lo = floorf(minValue / step) * step;
    t.hi = ceilf(maxValue / step) * step;
    if (t.hi <= t.lo) t.hi = t.lo + step;
    t.count = (int)lroundf((t.hi - t.lo) / step) + 1;
    if (t.count <= maxTicks) break;
    step = niceStep(step * 1.01f);   // rounding pushed it over: go up one nice size
  }
  t.step = step;
  return t;
}

int labelDecimals(float step, float largestValue) {
  float biggest = fabsf(largestValue);
  if (biggest < 1000.0f) return 2;   // reads like a price: 157.50
  bool stepHasFraction = fmodf(step, 1.0f) > 0.001f;
  if (biggest < 10000.0f && stepHasFraction) return 1;   // 1232.5
  return 0;                                              // 1235, 12500
}

int16_t mapToPixel(float value, float lo, float hi, int16_t pixelLo, int16_t pixelHi) {
  if (hi - lo == 0) return pixelLo;
  float fraction = (value - lo) / (hi - lo);
  return (int16_t)lroundf(pixelLo + fraction * (pixelHi - pixelLo));
}
