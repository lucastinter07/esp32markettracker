// Runs on the PC, not the ESP32: pio test -e native
#include <unity.h>
#include "price_targets.h"

void setUp() {}
void tearDown() {}

static const int32_t DAY = 20404;

void test_above_fires_once_when_reached() {
  PriceTarget t = {250.0f, 0};
  TEST_ASSERT_FALSE(checkTarget(t, true, 249.99f, DAY));
  TEST_ASSERT_TRUE(checkTarget(t, true, 250.0f, DAY));    // at the target counts
  TEST_ASSERT_EQUAL(DAY, t.hitDay);
  TEST_ASSERT_FALSE(checkTarget(t, true, 251.0f, DAY));   // one-shot
  TEST_ASSERT_FALSE(checkTarget(t, true, 260.0f, DAY + 1));   // still hit the next day
}

void test_below_fires_when_price_falls_to_it() {
  PriceTarget t = {180.0f, 0};
  TEST_ASSERT_FALSE(checkTarget(t, false, 185.0f, DAY));
  TEST_ASSERT_TRUE(checkTarget(t, false, 179.5f, DAY));
  TEST_ASSERT_FALSE(targetArmed(t));
  TEST_ASSERT_TRUE(targetSet(t));   // kept, so the editor can show "hit"
}

void test_no_target_never_fires() {
  PriceTarget t = {0, 0};
  TEST_ASSERT_FALSE(checkTarget(t, true, 1000.0f, DAY));
  TEST_ASSERT_FALSE(checkTarget(t, false, 0.01f, DAY));
}

void test_unknown_price_never_fires() {
  PriceTarget t = {10.0f, 0};
  TEST_ASSERT_FALSE(checkTarget(t, false, 0.0f, DAY));   // "no price yet" isn't a fall to 0
}

void test_setting_again_rearms() {
  PriceTarget t = {250.0f, DAY};
  TEST_ASSERT_FALSE(targetArmed(t));
  t = PriceTarget{260.0f, 0};
  TEST_ASSERT_TRUE(checkTarget(t, true, 261.0f, DAY + 2));
}

void test_valid_side_of_the_price() {
  TEST_ASSERT_TRUE(targetValid(250.0f, true, 240.0f));
  TEST_ASSERT_FALSE(targetValid(230.0f, true, 240.0f));    // "above" under the price
  TEST_ASSERT_FALSE(targetValid(240.0f, true, 240.0f));    // at the price: would fire at once
  TEST_ASSERT_TRUE(targetValid(200.0f, false, 240.0f));
  TEST_ASSERT_FALSE(targetValid(250.0f, false, 240.0f));
  TEST_ASSERT_TRUE(targetValid(5.0f, false, 0.0f));         // price unknown: accept
  TEST_ASSERT_FALSE(targetValid(0.0f, true, 100.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_above_fires_once_when_reached);
  RUN_TEST(test_below_fires_when_price_falls_to_it);
  RUN_TEST(test_no_target_never_fires);
  RUN_TEST(test_unknown_price_never_fires);
  RUN_TEST(test_setting_again_rearms);
  RUN_TEST(test_valid_side_of_the_price);
  return UNITY_END();
}
