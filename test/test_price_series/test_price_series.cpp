// Runs on the PC, not the ESP32: pio test -e native
#include <unity.h>
#include "price_series.h"

void setUp() {}
void tearDown() {}

void test_starts_empty() {
  PriceSeries s;
  TEST_ASSERT_TRUE(s.empty());
  TEST_ASSERT_EQUAL(0, s.size());
}

void test_keeps_oldest_to_newest_order() {
  PriceSeries s;
  s.push(1.0f);
  s.push(2.0f);
  s.push(3.0f);
  TEST_ASSERT_EQUAL(3, s.size());
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.first());
  TEST_ASSERT_EQUAL_FLOAT(3.0f, s.last());
}

void test_overwrites_oldest_when_full() {
  PriceSeries s;
  for (size_t i = 0; i < PriceSeries::CAPACITY + 10; i++) s.push((float)i);
  TEST_ASSERT_EQUAL(PriceSeries::CAPACITY, s.size());
  TEST_ASSERT_EQUAL_FLOAT(10.0f, s.first());   // first 10 points were pushed out
  TEST_ASSERT_EQUAL_FLOAT((float)(PriceSeries::CAPACITY + 9), s.last());
}

void test_min_max() {
  PriceSeries s;
  s.push(5.0f);
  s.push(2.5f);
  s.push(9.0f);
  s.push(4.0f);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, s.minValue());
  TEST_ASSERT_EQUAL_FLOAT(9.0f, s.maxValue());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_empty);
  RUN_TEST(test_keeps_oldest_to_newest_order);
  RUN_TEST(test_overwrites_oldest_when_full);
  RUN_TEST(test_min_max);
  return UNITY_END();
}
