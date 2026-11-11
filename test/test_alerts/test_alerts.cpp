// Runs on the PC, not the ESP32: pio test -e native
#include <unity.h>
#include "alerts.h"

void setUp() {}
void tearDown() {}

static const int32_t DAY = 20400;

void test_no_alert_inside_the_threshold() {
  AlertState s;
  TEST_ASSERT_EQUAL(AlertDirection::None, checkAlert(s, 2.9f, 3, DAY));
  TEST_ASSERT_EQUAL(AlertDirection::None, checkAlert(s, -2.9f, 3, DAY));
}

void test_up_fires_once_per_day() {
  AlertState s;
  TEST_ASSERT_EQUAL(AlertDirection::Up, checkAlert(s, 3.0f, 3, DAY));
  TEST_ASSERT_EQUAL(AlertDirection::None, checkAlert(s, 4.5f, 3, DAY));
  TEST_ASSERT_EQUAL(AlertDirection::None, checkAlert(s, 2.0f, 3, DAY));
  TEST_ASSERT_EQUAL(AlertDirection::None, checkAlert(s, 3.5f, 3, DAY));   // back above: still once
}

void test_down_is_separate_from_up() {
  AlertState s;
  TEST_ASSERT_EQUAL(AlertDirection::Up, checkAlert(s, 5.1f, 5, DAY));
  TEST_ASSERT_EQUAL(AlertDirection::Down, checkAlert(s, -5.0f, 5, DAY));
  TEST_ASSERT_EQUAL(AlertDirection::None, checkAlert(s, -6.0f, 5, DAY));
}

void test_new_day_rearms() {
  AlertState s;
  TEST_ASSERT_EQUAL(AlertDirection::Down, checkAlert(s, -2.5f, 2, DAY));
  TEST_ASSERT_EQUAL(AlertDirection::Down, checkAlert(s, -2.5f, 2, DAY + 1));
}

void test_threshold_zero_means_off() {
  AlertState s;
  TEST_ASSERT_EQUAL(AlertDirection::None, checkAlert(s, 50.0f, 0, DAY));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_alert_inside_the_threshold);
  RUN_TEST(test_up_fires_once_per_day);
  RUN_TEST(test_down_is_separate_from_up);
  RUN_TEST(test_new_day_rearms);
  RUN_TEST(test_threshold_zero_means_off);
  return UNITY_END();
}
