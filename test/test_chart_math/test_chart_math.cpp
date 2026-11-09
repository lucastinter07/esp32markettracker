// Runs on the PC, not the ESP32: pio test -e native
#include <unity.h>
#include "chart_math.h"
#include "price_series.h"

void setUp() {}
void tearDown() {}

void test_nice_axis_round_steps() {
  AxisTicks t = niceAxis(155.13f, 159.71f, 5);   // a typical day for a ~$157 stock
  // Gives 154 / 156 / 158 / 160: covers the data, round values, at most 5 labels
  TEST_ASSERT_EQUAL_FLOAT(154.0f, t.lo);
  TEST_ASSERT_EQUAL_FLOAT(160.0f, t.hi);
  TEST_ASSERT_EQUAL(4, t.count);
  // the step is a "nice" number
  TEST_ASSERT_TRUE(t.step == 1.0f || t.step == 2.0f || t.step == 2.5f || t.step == 5.0f);
}

void test_nice_axis_covers_the_data() {
  float lows[] = {0.37f, 22.85f, 733.0f, 4823.0f};
  float highs[] = {0.41f, 23.40f, 765.97f, 5102.0f};
  for (int i = 0; i < 4; i++) {
    AxisTicks t = niceAxis(lows[i], highs[i], 5);
    TEST_ASSERT_TRUE(t.lo <= lows[i]);
    TEST_ASSERT_TRUE(t.hi >= highs[i]);
    TEST_ASSERT_TRUE(t.count >= 2 && t.count <= 5);
  }
}

void test_nice_axis_flat_line() {
  AxisTicks t = niceAxis(100.0f, 100.0f, 5);   // every price identical
  TEST_ASSERT_TRUE(t.hi > t.lo);
  TEST_ASSERT_TRUE(t.lo <= 100.0f && t.hi >= 100.0f);
}

void test_label_decimals() {
  TEST_ASSERT_EQUAL(2, labelDecimals(2.5f, 160.0f));
  TEST_ASSERT_EQUAL(1, labelDecimals(2.5f, 1235.0f));
  TEST_ASSERT_EQUAL(0, labelDecimals(5.0f, 1235.0f));
  TEST_ASSERT_EQUAL(0, labelDecimals(50.0f, 12500.0f));
}

void test_map_to_pixel_both_directions() {
  TEST_ASSERT_EQUAL(100, mapToPixel(150.0f, 100.0f, 200.0f, 50, 150));
  TEST_ASSERT_EQUAL(200, mapToPixel(100.0f, 100.0f, 200.0f, 200, 100));   // screen y counts down
  TEST_ASSERT_EQUAL(100, mapToPixel(200.0f, 100.0f, 200.0f, 200, 100));
}

void test_series_keeps_times() {
  PriceSeries s;
  s.push(10.0f, 1000);
  s.push(12.0f, 1060);
  s.push(9.0f, 1120);
  TEST_ASSERT_EQUAL_UINT32(1060, s.timeAt(1));
  TEST_ASSERT_EQUAL(2, s.indexOfMin());
  TEST_ASSERT_EQUAL(1, s.indexOfMax());
  TEST_ASSERT_TRUE(s.hasTimes());
  s.push(11.0f);   // no time (demo prices)
  TEST_ASSERT_FALSE(s.hasTimes());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nice_axis_round_steps);
  RUN_TEST(test_nice_axis_covers_the_data);
  RUN_TEST(test_nice_axis_flat_line);
  RUN_TEST(test_label_decimals);
  RUN_TEST(test_map_to_pixel_both_directions);
  RUN_TEST(test_series_keeps_times);
  return UNITY_END();
}
