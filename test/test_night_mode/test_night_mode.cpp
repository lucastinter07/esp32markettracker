// Runs on the PC, not the ESP32: pio test -e native
#include <unity.h>
#include "night_mode.h"

void setUp() {}
void tearDown() {}

void test_night_wraps_past_midnight() {
  TEST_ASSERT_TRUE(isNightHour(22, 22, 7));
  TEST_ASSERT_TRUE(isNightHour(23, 22, 7));
  TEST_ASSERT_TRUE(isNightHour(0, 22, 7));
  TEST_ASSERT_TRUE(isNightHour(6, 22, 7));
  TEST_ASSERT_FALSE(isNightHour(7, 22, 7));    // 07:00 is day again
  TEST_ASSERT_FALSE(isNightHour(21, 22, 7));
  TEST_ASSERT_FALSE(isNightHour(12, 22, 7));
}

void test_night_starting_at_midnight() {
  TEST_ASSERT_TRUE(isNightHour(0, 0, 6));
  TEST_ASSERT_TRUE(isNightHour(5, 0, 6));
  TEST_ASSERT_FALSE(isNightHour(6, 0, 6));
  TEST_ASSERT_FALSE(isNightHour(23, 0, 6));
}

void test_equal_hours_mean_no_night() {
  TEST_ASSERT_FALSE(isNightHour(3, 5, 5));
}

void test_brightness_dims_only_at_night() {
  TEST_ASSERT_EQUAL(80, targetBrightness(80, true, false, false));   // day
  TEST_ASSERT_EQUAL(50, targetBrightness(80, true, true, false));    // night
  TEST_ASSERT_EQUAL(80, targetBrightness(80, true, true, true));     // touched: awake
  TEST_ASSERT_EQUAL(80, targetBrightness(80, false, true, false));   // dimming off
  TEST_ASSERT_EQUAL(20, targetBrightness(20, true, true, false));    // already dimmer
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_night_wraps_past_midnight);
  RUN_TEST(test_night_starting_at_midnight);
  RUN_TEST(test_equal_hours_mean_no_night);
  RUN_TEST(test_brightness_dims_only_at_night);
  return UNITY_END();
}
