// Runs on the PC, not the ESP32: pio test -e native
// Metrics JSON is a trimmed real Finnhub response for NVDA (the real one is ~196 KB).
#include <unity.h>
#include "metrics_parser.h"
#include "symbol_state.h"

void setUp() {}
void tearDown() {}

static const char* NVDA_METRICS =
    "{\"metric\":{\"10DayAverageTradingVolume\":108.04599,\"52WeekHigh\":243.37,"
    "\"52WeekHighDate\":\"2026-10-06\",\"52WeekLow\":164.27,\"epsTTM\":7.9108,"
    "\"marketCapitalization\":4823133,\"peTTM\":25.006},\"metricType\":\"all\","
    "\"series\":{\"annual\":{\"eps\":[{\"period\":\"2026-01-25\",\"v\":4.8979}]}},"
    "\"symbol\":\"NVDA\"}";

void test_parses_metrics_and_skips_the_rest() {
  MetricData m;
  TEST_ASSERT_TRUE(parseMetrics(NVDA_METRICS, m));
  TEST_ASSERT_EQUAL_FLOAT(243.37f, m.high52);
  TEST_ASSERT_EQUAL_FLOAT(164.27f, m.low52);
  TEST_ASSERT_EQUAL_FLOAT(7.9108f, m.epsTTM);
  TEST_ASSERT_EQUAL_FLOAT(4823133.0f, m.marketCapM);
}

void test_rejects_unknown_symbol() {
  MetricData m;   // Finnhub's real answer for a symbol it doesn't know:
  TEST_ASSERT_FALSE(parseMetrics("{\"metric\":{},\"metricType\":\"all\",\"series\":{},\"symbol\":\"ZZZZZZ\"}", m));
  TEST_ASSERT_FALSE(parseMetrics("not json", m));
}

static SymbolState stockAt(float price) {
  SymbolState s;
  Quote q;
  q.price = price;
  q.high = price;
  q.low = price;
  s.applyQuote(q, 0);
  return s;
}

void test_pe_and_market_cap_follow_the_price() {
  SymbolState s = stockAt(200.0f);
  MetricData m;
  m.high52 = 243.37f;
  m.low52 = 164.27f;
  m.epsTTM = 8.0f;
  m.marketCapM = 4800000.0f;   // $4.8 trillion at $200 = 24,000 million shares
  s.applyMetrics(m, 20000);

  TEST_ASSERT_EQUAL_FLOAT(25.0f, s.peRatio());               // 200 / 8
  TEST_ASSERT_FLOAT_WITHIN(1e9, 4.8e12, s.marketCap());

  Quote q = s.quote;
  q.price = 220.0f;   // price rises 10% ...
  s.applyQuote(q, 1000);
  TEST_ASSERT_EQUAL_FLOAT(27.5f, s.peRatio());               // ... so does P/E
  TEST_ASSERT_FLOAT_WITHIN(1e9, 5.28e12, s.marketCap());     // ... and market cap
}

void test_negative_earnings_have_no_pe() {
  SymbolState s = stockAt(50.0f);
  MetricData m;
  m.high52 = 60.0f;
  m.low52 = 20.0f;
  m.epsTTM = -1.5f;
  s.applyMetrics(m, 20000);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.peRatio());
}

void test_52_week_range_widens_with_new_highs_and_lows() {
  SymbolState s = stockAt(200.0f);
  MetricData m;
  m.high52 = 243.37f;
  m.low52 = 164.27f;
  s.applyMetrics(m, 20000);

  Quote q = s.quote;
  q.high = 250.0f;   // a new 52-week high during the day
  q.low = 199.0f;
  s.applyQuote(q, 1000);
  TEST_ASSERT_EQUAL_FLOAT(250.0f, s.fundamentals.high52);
  TEST_ASSERT_EQUAL_FLOAT(164.27f, s.fundamentals.low52);   // unchanged

  q.low = 150.0f;
  s.applyQuote(q, 2000);
  TEST_ASSERT_EQUAL_FLOAT(150.0f, s.fundamentals.low52);
}

// Regression (2026-11-09): just after 9:30 a quote's last-trade time can still be
// Friday's, which put the whole chart on Friday's session. Points must use the
// fetch time instead.
void test_chart_point_uses_fetch_time_not_trade_time() {
  SymbolState s;
  Quote q;
  q.price = 100.0f;
  q.timestamp = 1793995200;   // Friday's last trade
  s.applyQuote(q, 0, true, 1794238200);   // fetched Monday 9:30
  TEST_ASSERT_EQUAL_UINT32(1794238200u, s.intraday.timeAt(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_chart_point_uses_fetch_time_not_trade_time);
  RUN_TEST(test_parses_metrics_and_skips_the_rest);
  RUN_TEST(test_rejects_unknown_symbol);
  RUN_TEST(test_pe_and_market_cap_follow_the_price);
  RUN_TEST(test_negative_earnings_have_no_pe);
  RUN_TEST(test_52_week_range_widens_with_new_highs_and_lows);
  return UNITY_END();
}
