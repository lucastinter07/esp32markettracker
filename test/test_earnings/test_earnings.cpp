// Runs on the PC, not the ESP32: pio test -e native
// JSON strings are real Finnhub /calendar/earnings responses.
#include <unity.h>
#include "earnings.h"
#include "market_calendar.h"

void setUp() {}
void tearDown() {}

void test_parses_real_reply() {
  Earnings e;
  const char* json =
      "{\"earningsCalendar\":[{\"symbol\":\"AMD\",\"date\":\"2027-02-01\",\"hour\":\"amc\",\"quarter\":4,"
      "\"year\":2026,\"epsEstimate\":2.9483,\"epsActual\":null,\"revenueEstimate\":17144952565,"
      "\"revenueActual\":null}]}";
  TEST_ASSERT_TRUE(parseEarnings(json, dayNumber(2026, 11, 9), e));
  TEST_ASSERT_EQUAL(dayNumber(2027, 2, 1), e.day);
  TEST_ASSERT_EQUAL_STRING("amc", e.hour);
  TEST_ASSERT_EQUAL_STRING("after close", earningsTimeText(e));
  TEST_ASSERT_EQUAL_FLOAT(2.9483f, e.epsEstimate);
}

void test_unannounced_time() {
  Earnings e;   // AAPL's reply had "hour":""
  TEST_ASSERT_TRUE(parseEarnings(
      "{\"earningsCalendar\":[{\"symbol\":\"AAPL\",\"date\":\"2027-01-27\",\"hour\":\"\",\"epsEstimate\":2.9621}]}",
      dayNumber(2026, 11, 9), e));
  TEST_ASSERT_EQUAL_STRING("", earningsTimeText(e));
}

void test_empty_list_is_valid_but_none() {
  Earnings e;   // ETFs and unknown symbols
  TEST_ASSERT_TRUE(parseEarnings("{\"earningsCalendar\":[]}", dayNumber(2026, 11, 9), e));
  TEST_ASSERT_EQUAL(0, e.day);
  TEST_ASSERT_FALSE(parseEarnings("not json", dayNumber(2026, 11, 9), e));
  TEST_ASSERT_FALSE(parseEarnings("{\"error\":\"x\"}", dayNumber(2026, 11, 9), e));
}

void test_picks_soonest_upcoming() {
  Earnings e;
  const char* json =
      "{\"earningsCalendar\":["
      "{\"date\":\"2027-04-28\",\"hour\":\"bmo\"},"
      "{\"date\":\"2026-10-28\",\"hour\":\"amc\"},"   // already happened
      "{\"date\":\"2027-01-27\",\"hour\":\"dmh\"}]}";
  TEST_ASSERT_TRUE(parseEarnings(json, dayNumber(2026, 11, 9), e));
  TEST_ASSERT_EQUAL(dayNumber(2027, 1, 27), e.day);
  TEST_ASSERT_EQUAL_STRING("during day", earningsTimeText(e));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_real_reply);
  RUN_TEST(test_unannounced_time);
  RUN_TEST(test_empty_list_is_valid_but_none);
  RUN_TEST(test_picks_soonest_upcoming);
  return UNITY_END();
}
