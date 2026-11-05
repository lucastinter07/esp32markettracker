// Runs on the PC, not the ESP32: pio test -e native
// Expected dates come from NYSE's published holiday calendars.
#include <unity.h>
#include "market_calendar.h"

void setUp() {}
void tearDown() {}

struct Ymd { int y, m, d; };

void test_weekday_and_easter() {
  TEST_ASSERT_EQUAL(3, weekdayOf(2026, 11, 4));   // Wednesday
  int m, d;
  easterSunday(2026, m, d);
  TEST_ASSERT_EQUAL(4, m);
  TEST_ASSERT_EQUAL(5, d);
  easterSunday(2027, m, d);
  TEST_ASSERT_EQUAL(3, m);
  TEST_ASSERT_EQUAL(28, d);
}

void test_holidays_2025_2026_2027() {
  const Ymd holidays[] = {
      {2025, 1, 1}, {2025, 1, 20}, {2025, 2, 17}, {2025, 4, 18}, {2025, 5, 26},
      {2025, 6, 19}, {2025, 7, 4}, {2025, 9, 1}, {2025, 11, 27}, {2025, 12, 25},
      {2026, 1, 1}, {2026, 1, 19}, {2026, 2, 16}, {2026, 4, 3}, {2026, 5, 25},
      {2026, 6, 19}, {2026, 7, 3}, {2026, 9, 7}, {2026, 11, 26}, {2026, 12, 25},
      {2027, 1, 1}, {2027, 1, 18}, {2027, 2, 15}, {2027, 3, 26}, {2027, 5, 31},
      {2027, 6, 18}, {2027, 7, 5}, {2027, 9, 6}, {2027, 11, 25}, {2027, 12, 24},
  };
  for (const Ymd& h : holidays) {
    TEST_ASSERT_TRUE_MESSAGE(isHoliday(h.y, h.m, h.d), "expected a holiday");
    TEST_ASSERT_FALSE(isTradingDay(h.y, h.m, h.d));
  }
  // Ordinary days and the real date of a moved holiday are not holidays
  TEST_ASSERT_FALSE(isHoliday(2026, 11, 4));
  TEST_ASSERT_FALSE(isHoliday(2026, 7, 4));    // Saturday; observed on Friday 7/3
  TEST_ASSERT_FALSE(isHoliday(2027, 12, 31));  // 2028 New Year falls on Saturday: no closure
}

void test_early_closes() {
  TEST_ASSERT_TRUE(isEarlyClose(2025, 7, 3));
  TEST_ASSERT_TRUE(isEarlyClose(2025, 11, 28));
  TEST_ASSERT_TRUE(isEarlyClose(2025, 12, 24));
  TEST_ASSERT_TRUE(isEarlyClose(2026, 11, 27));
  TEST_ASSERT_TRUE(isEarlyClose(2026, 12, 24));
  TEST_ASSERT_TRUE(isEarlyClose(2027, 11, 26));
  TEST_ASSERT_FALSE(isEarlyClose(2026, 7, 2));
  TEST_ASSERT_FALSE(isEarlyClose(2026, 11, 4));
}

void test_status_open_with_minutes_to_close() {
  MarketStatus s = marketStatus({2026, 11, 4, 10, 0});
  TEST_ASSERT_TRUE(s.phase == MarketPhase::Open);
  TEST_ASSERT_EQUAL(360, s.minutesToClose);
}

void test_status_premarket_opens_today() {
  MarketStatus s = marketStatus({2026, 11, 4, 8, 0});
  TEST_ASSERT_TRUE(s.phase == MarketPhase::PreMarket);
  TEST_ASSERT_TRUE(s.nextOpenIsToday);
  TEST_ASSERT_EQUAL(90, s.minutesToOpen);
}

void test_status_after_hours_opens_tomorrow() {
  MarketStatus s = marketStatus({2026, 11, 4, 17, 0});
  TEST_ASSERT_TRUE(s.phase == MarketPhase::AfterHours);
  TEST_ASSERT_FALSE(s.nextOpenIsToday);
  TEST_ASSERT_EQUAL(4, s.nextOpenWeekday);   // Thursday
  TEST_ASSERT_EQUAL(990, s.minutesToOpen);
}

void test_status_friday_night_opens_monday() {
  MarketStatus s = marketStatus({2026, 11, 6, 21, 0});
  TEST_ASSERT_TRUE(s.phase == MarketPhase::Closed);
  TEST_ASSERT_EQUAL(1, s.nextOpenWeekday);   // Monday
  TEST_ASSERT_EQUAL(3 * 1440 + 570 - 1260, s.minutesToOpen);
}

void test_status_holiday_and_early_close() {
  MarketStatus thanksgiving = marketStatus({2026, 11, 26, 12, 0});
  TEST_ASSERT_TRUE(thanksgiving.phase == MarketPhase::Closed);
  TEST_ASSERT_TRUE(thanksgiving.holiday);
  TEST_ASSERT_EQUAL(5, thanksgiving.nextOpenWeekday);   // Friday

  MarketStatus dayAfter = marketStatus({2026, 11, 27, 12, 0});
  TEST_ASSERT_TRUE(dayAfter.phase == MarketPhase::Open);
  TEST_ASSERT_TRUE(dayAfter.earlyClose);
  TEST_ASSERT_EQUAL(60, dayAfter.minutesToClose);

  MarketStatus afterEarly = marketStatus({2026, 11, 27, 14, 0});
  TEST_ASSERT_TRUE(afterEarly.phase == MarketPhase::AfterHours);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_weekday_and_easter);
  RUN_TEST(test_holidays_2025_2026_2027);
  RUN_TEST(test_early_closes);
  RUN_TEST(test_status_open_with_minutes_to_close);
  RUN_TEST(test_status_premarket_opens_today);
  RUN_TEST(test_status_after_hours_opens_tomorrow);
  RUN_TEST(test_status_friday_night_opens_monday);
  RUN_TEST(test_status_holiday_and_early_close);
  return UNITY_END();
}
