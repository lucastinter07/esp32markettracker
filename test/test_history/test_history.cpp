// Runs on the PC, not the ESP32: pio test -e native
// CSV lines are real Twelve Data responses (QQQ daily, SPY 30-minute in UTC).
#include <unity.h>
#include "history.h"
#include "market_calendar.h"
#include "price_series.h"

void setUp() {}
void tearDown() {}

static const char* QQQ_DAILY =
    "datetime;open;high;low;close;volume\n"
    "2026-11-06;658.34998;664.52002;653.98999;656.099976;70469100\n"
    "2026-11-05;683.12000;683.65002;647.049988;647.049988;130677400\n"
    "2026-11-04;688.42999;701.45001;687.87000;691.95001;62365900\n"
    "2026-11-03;684.39001;688.69000;683.17999;685.28003;37524400\n"
    "2026-11-02;695.32001;699.42999;680.33002;681.090027;54743700\n"
    "2026-10-30;697.21002;698.63000;683.45001;690.48999;85242100\n"
    "2026-10-29;716.53998;718.070007;711.62000;715.34003;38251600\n";

static const char* SPY_BARS =
    "datetime;open;high;low;close;volume\n"
    "2026-11-06 20:30:00;737.96002;738.030029;735.07;735.34003;9769519\n"
    "2026-11-06 20:00:00;738.039978;739.11;737.39001;737.96997;43335562\n"
    "2026-11-06 19:30:00;737.89001;738.29;737.01;738.049988;2059613\n";

void test_parses_daily_oldest_first() {
  History h;
  TEST_ASSERT_TRUE(parseDailyCsv(QQQ_DAILY, h));
  TEST_ASSERT_EQUAL(7, h.dailyCount);
  TEST_ASSERT_EQUAL(dayNumber(2026, 10, 29), h.dailyDay[0]);   // oldest first
  TEST_ASSERT_EQUAL_FLOAT(715.34003f, h.dailyClose[0]);
  TEST_ASSERT_EQUAL(dayNumber(2026, 11, 6), h.dailyDay[6]);
  TEST_ASSERT_EQUAL_FLOAT(656.099976f, h.dailyClose[6]);
}

void test_parses_bars_as_end_times() {
  History h;
  TEST_ASSERT_TRUE(parseBarsCsv(SPY_BARS, h));
  TEST_ASSERT_EQUAL(3, h.barCount);
  // The newest bar starts 20:30 UTC (15:30 New York) and ends 21:00 UTC (16:00 = the close).
  uint32_t expected = (uint32_t)dayNumber(2026, 11, 6) * 86400u + 21 * 3600;
  TEST_ASSERT_EQUAL_UINT32(expected, h.barTime[2]);
  TEST_ASSERT_EQUAL_FLOAT(735.34003f, h.barClose[2]);
  TEST_ASSERT_TRUE(h.barTime[0] < h.barTime[1]);
}

void test_rejects_error_replies() {
  History h;
  const char* err = "{\"code\":404,\"message\":\"symbol is missing or invalid\",\"status\":\"error\"}";
  TEST_ASSERT_FALSE(parseDailyCsv(err, h));
  TEST_ASSERT_FALSE(parseBarsCsv(err, h));
  TEST_ASSERT_FALSE(parseDailyCsv("datetime;open;high;low;close;volume\n", h));   // no rows
}

void test_change_over_sessions() {
  History h;
  parseDailyCsv(QQQ_DAILY, h);
  float pct = 0;
  // Monday 11-09 session, price 700: 5 sessions back from Monday is Mon 11-02 (681.09)
  TEST_ASSERT_TRUE(changeOverSessions(h, dayNumber(2026, 11, 9), 700.0f, 5, pct));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, (700.0f - 681.090027f) / 681.090027f * 100.0f, pct);
  // Not enough history for a month yet
  TEST_ASSERT_FALSE(changeOverSessions(h, dayNumber(2026, 11, 9), 700.0f, 21, pct));
  // During Friday 11-06's own session, Friday's row is ignored: 1 back is Thu 11-05
  TEST_ASSERT_TRUE(changeOverSessions(h, dayNumber(2026, 11, 6), 647.049988f, 1, pct));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, pct);
}

void test_last_completed_session_day() {
  // Wednesday 11-04: before 16:15 the last finished session is Tuesday; after, Wednesday
  TEST_ASSERT_EQUAL(dayNumber(2026, 11, 3), lastCompletedSessionDay({2026, 11, 4, 16, 0}));
  TEST_ASSERT_EQUAL(dayNumber(2026, 11, 4), lastCompletedSessionDay({2026, 11, 4, 16, 15}));
  // Saturday -> Friday; early close day 11-27 counts from 13:15
  TEST_ASSERT_EQUAL(dayNumber(2026, 11, 6), lastCompletedSessionDay({2026, 11, 7, 9, 0}));
  TEST_ASSERT_EQUAL(dayNumber(2026, 11, 27), lastCompletedSessionDay({2026, 11, 27, 13, 15}));
}

void test_close_sessions_back() {
  History h;
  parseDailyCsv(QQQ_DAILY, h);
  float close = 0;
  TEST_ASSERT_TRUE(closeSessionsBack(h, dayNumber(2026, 11, 9), 1, close));   // Friday
  TEST_ASSERT_EQUAL_FLOAT(656.099976f, close);
  TEST_ASSERT_TRUE(closeSessionsBack(h, dayNumber(2026, 11, 9), 5, close));   // Mon 11-02
  TEST_ASSERT_EQUAL_FLOAT(681.090027f, close);
  TEST_ASSERT_FALSE(closeSessionsBack(h, dayNumber(2026, 11, 9), 8, close));  // only 7 rows
}

void test_five_minute_bars() {
  uint32_t times[4];
  float prices[4];
  const char* csv =
      "datetime;open;high;low;close;volume\n"
      "2026-11-09 14:40:00;510;511;509;510.5;1000\n"
      "2026-11-09 14:35:00;509;510;508;509.5;1000\n";
  TEST_ASSERT_EQUAL(2, parseBarsCsvInto(csv, times, prices, 4, 300));
  uint32_t day = (uint32_t)dayNumber(2026, 11, 9) * 86400u;
  TEST_ASSERT_EQUAL_UINT32(day + 14 * 3600 + 40 * 60, times[0]);   // 14:35 bar ends 14:40
  TEST_ASSERT_EQUAL_FLOAT(510.5f, prices[1]);
}

void test_fill_gaps_morning_middle_and_end() {
  // Session 0..3000 s. Live points only from 1000 to 1060 (the tracker was off otherwise).
  PriceSeries s;
  s.push(105.0f, 1000);
  s.push(106.0f, 1060);
  TEST_ASSERT_TRUE(s.hasGap(0, 3000, 300));
  // 5-minute bars: some before, some right next to the live points, some after
  uint32_t times[] = {300, 600, 900, 1020, 1500, 1800, 3000, 3300};
  float prices[] = {101.0f, 102.0f, 103.0f, 999.0f, 107.0f, 108.0f, 109.0f, 555.0f};
  s.fillGaps(times, prices, 8, 0, 3000, 120);
  // Kept: 300, 600 | live 1000, 1060 | 1500, 1800, 3000.
  // Dropped: 900 and 1020 (within 120 s of a live point), 3300 (after the session).
  TEST_ASSERT_EQUAL(7, s.size());
  TEST_ASSERT_EQUAL_UINT32(300u, s.timeAt(0));
  TEST_ASSERT_EQUAL_FLOAT(105.0f, s.at(2));   // live points kept, in order
  TEST_ASSERT_EQUAL_FLOAT(106.0f, s.at(3));
  TEST_ASSERT_EQUAL_UINT32(3000u, s.timeAt(6));
  for (size_t i = 1; i < s.size(); i++) TEST_ASSERT_TRUE(s.timeAt(i) > s.timeAt(i - 1));
  float ref = 0;
  TEST_ASSERT_TRUE(s.firstAtOrAfter(650, ref));
  TEST_ASSERT_EQUAL_FLOAT(105.0f, ref);   // 900 was dropped, so the live point at 1000
}

void test_has_gap() {
  PriceSeries s;
  TEST_ASSERT_TRUE(s.hasGap(0, 1000, 300));   // empty
  for (uint32_t t = 60; t <= 1000; t += 60) s.push(100.0f, t);   // a point every minute
  TEST_ASSERT_FALSE(s.hasGap(0, 1000, 300));
  TEST_ASSERT_TRUE(s.hasGap(0, 2000, 300));    // nothing after 1000: hole at the end
  PriceSeries h;
  h.push(1.0f, 100);
  h.push(2.0f, 900);   // 800 s between points
  TEST_ASSERT_TRUE(h.hasGap(0, 1000, 300));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_close_sessions_back);
  RUN_TEST(test_five_minute_bars);
  RUN_TEST(test_fill_gaps_morning_middle_and_end);
  RUN_TEST(test_has_gap);
  RUN_TEST(test_parses_daily_oldest_first);
  RUN_TEST(test_parses_bars_as_end_times);
  RUN_TEST(test_rejects_error_replies);
  RUN_TEST(test_change_over_sessions);
  RUN_TEST(test_last_completed_session_day);
  return UNITY_END();
}
