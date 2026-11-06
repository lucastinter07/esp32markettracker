// Runs on the PC, not the ESP32: pio test -e native
// The JSON strings are real Finnhub responses.
#include <unity.h>
#include "quote_parser.h"

void setUp() {}
void tearDown() {}

void test_parses_real_quote() {
  Quote q;
  const char* json =
      "{\"c\":733,\"d\":-33.81,\"dp\":-4.4092,\"h\":765.97,\"l\":732.71,"
      "\"o\":765.47,\"pc\":766.81,\"t\":1793912400}";
  TEST_ASSERT_TRUE(parseQuote(json, q));
  TEST_ASSERT_EQUAL_FLOAT(733.0f, q.price);
  TEST_ASSERT_EQUAL_FLOAT(-33.81f, q.change);
  TEST_ASSERT_EQUAL_FLOAT(-4.4092f, q.changePct);
  TEST_ASSERT_EQUAL_FLOAT(765.97f, q.high);
  TEST_ASSERT_EQUAL_FLOAT(732.71f, q.low);
  TEST_ASSERT_EQUAL_FLOAT(765.47f, q.open);
  TEST_ASSERT_EQUAL_FLOAT(766.81f, q.prevClose);
  TEST_ASSERT_EQUAL_UINT32(1793912400u, q.timestamp);
}

void test_rejects_unknown_symbol() {
  Quote q;
  TEST_ASSERT_FALSE(parseQuote(
      "{\"c\":0,\"d\":null,\"dp\":null,\"h\":0,\"l\":0,\"o\":0,\"pc\":0,\"t\":0}", q));
}

void test_rejects_bad_json() {
  Quote q;
  TEST_ASSERT_FALSE(parseQuote("", q));
  TEST_ASSERT_FALSE(parseQuote("{\"c\":733,", q));               // cut off mid-response
  TEST_ASSERT_FALSE(parseQuote("{\"error\":\"bad token\"}", q));  // no price at all
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_real_quote);
  RUN_TEST(test_rejects_unknown_symbol);
  RUN_TEST(test_rejects_bad_json);
  return UNITY_END();
}
