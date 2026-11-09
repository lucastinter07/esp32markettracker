// Math for chart axes: picks "nice" round label values (155.00, 157.50, 160.00 rather
// than 155.13, 157.71, ...). Pure C++, unit-tested on the PC.
#pragma once
#include <stdint.h>

struct AxisTicks {
  float lo;      // first label (at or below the lowest price)
  float hi;      // last label (at or above the highest price)
  float step;    // distance between labels: 1, 2, 2.5 or 5 times a power of ten
  int count;     // number of labels, including lo and hi
};

// Labels covering [minValue, maxValue] with at most maxTicks labels.
AxisTicks niceAxis(float minValue, float maxValue, int maxTicks);

// How many decimals the labels show: 2 below $1000 (so they read like prices), fewer for
// big prices so a label stays at most 6 characters (1232.5, 12500).
int labelDecimals(float step, float largestValue);

// Where a value lands on a pixel range: lo maps to pixelLo, hi to pixelHi.
// Works in either direction, so screen y (which counts down) is pixelLo = bottom.
int16_t mapToPixel(float value, float lo, float hi, int16_t pixelLo, int16_t pixelHi);
