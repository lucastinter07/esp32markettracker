// Runs on the PC, not the ESP32: pio test -e native
// The JSON is Finnhub's real /stock/profile2 response for AMD.
#include <unity.h>
#include <string.h>
#include "profile_parser.h"

void setUp() {}
void tearDown() {}

void test_parses_real_profile() {
  Profile p;
  const char* json =
      "{\"ticker\":\"AMD\",\"name\":\"Advanced Micro Devices, Inc.\",\"country\":\"US\","
      "\"currency\":\"USD\",\"exchange\":\"NASDAQ NMS - GLOBAL MARKET\",\"ipo\":\"1979-10-15\","
      "\"marketCapitalization\":733046.5612062463,\"shareOutstanding\":1632.48,"
      "\"finnhubIndustry\":\"Semiconductors\",\"weburl\":\"https://www.amd.com/\"}";
  TEST_ASSERT_TRUE(parseProfile(json, p));
  TEST_ASSERT_EQUAL_STRING("Advanced Micro Devices, Inc.", p.name);
  TEST_ASSERT_EQUAL_STRING("Semiconductors", p.industry);
}

void test_rejects_empty_profile() {
  Profile p;   // Finnhub's answer for unknown symbols and for ETFs like SPY
  TEST_ASSERT_FALSE(parseProfile("{}", p));
  TEST_ASSERT_FALSE(parseProfile("not json", p));
}

void test_cuts_long_text_safely() {
  Profile p;
  // 60-character name: longer than the 48-byte field
  const char* json =
      "{\"name\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ12345678\","
      "\"finnhubIndustry\":\"X\"}";
  TEST_ASSERT_TRUE(parseProfile(json, p));
  TEST_ASSERT_EQUAL(47, strlen(p.name));   // cut to fit, still a valid C string
}

void test_apply_profile_marks_valid_and_dated() {
  SymbolState s;
  Profile p;
  TEST_ASSERT_TRUE(parseProfile("{\"name\":\"Apple Inc.\",\"finnhubIndustry\":\"Technology\"}", p));
  uint32_t before = s.version;
  s.applyProfile(p, 20765);
  TEST_ASSERT_TRUE(s.profile.valid);
  TEST_ASSERT_EQUAL(20765, s.profile.fetchedDay);
  TEST_ASSERT_TRUE(s.version != before);   // screens will redraw with the name
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_real_profile);
  RUN_TEST(test_rejects_empty_profile);
  RUN_TEST(test_cuts_long_text_safely);
  RUN_TEST(test_apply_profile_marks_valid_and_dated);
  return UNITY_END();
}
