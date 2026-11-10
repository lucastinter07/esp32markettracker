// Runs on the PC, not the ESP32: pio test -e native
#include <unity.h>
#include "symbol_state.h"
#include "symbol_text.h"

void setUp() {}
void tearDown() {}

void test_accepts_normal_tickers() {
  TEST_ASSERT_TRUE(isValidSymbol("A"));
  TEST_ASSERT_TRUE(isValidSymbol("NVDA"));
  TEST_ASSERT_TRUE(isValidSymbol("BRK.B"));
  TEST_ASSERT_TRUE(isValidSymbol("ABCDEFG"));   // 7 = the longest stored
}

void test_rejects_bad_text() {
  TEST_ASSERT_FALSE(isValidSymbol(""));
  TEST_ASSERT_FALSE(isValidSymbol("ABCDEFGH"));   // too long
  TEST_ASSERT_FALSE(isValidSymbol("nvda"));
  TEST_ASSERT_FALSE(isValidSymbol(".A"));
  TEST_ASSERT_FALSE(isValidSymbol("A."));
  TEST_ASSERT_FALSE(isValidSymbol("A-B"));
}

void test_finds_duplicates() {
  const char* list[] = {"NVDA", "AMD", "INTC"};
  TEST_ASSERT_EQUAL(1, findSymbol(list, 3, "AMD"));
  TEST_ASSERT_EQUAL(-1, findSymbol(list, 3, "AM"));
}

void test_clear_data_forgets_the_old_ticker() {
  SymbolState s;
  Quote q;
  q.price = 100;
  s.applyQuote(q, 1000, true, 1700000000);
  Profile p;
  s.applyProfile(p, 20000);
  s.history.valid = true;
  s.earnings.valid = true;
  s.fundamentals.valid = true;
  uint32_t before = s.version;

  s.clearData();
  TEST_ASSERT_FALSE(s.hasData);
  TEST_ASSERT_EQUAL(0, s.intraday.size());
  TEST_ASSERT_FALSE(s.profile.valid);
  TEST_ASSERT_FALSE(s.history.valid);
  TEST_ASSERT_FALSE(s.earnings.valid);
  TEST_ASSERT_FALSE(s.fundamentals.valid);
  TEST_ASSERT_EQUAL_FLOAT(0, s.quote.price);
  TEST_ASSERT_TRUE(s.version > before);   // screens redraw the now-empty row
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_accepts_normal_tickers);
  RUN_TEST(test_rejects_bad_text);
  RUN_TEST(test_finds_duplicates);
  RUN_TEST(test_clear_data_forgets_the_old_ticker);
  return UNITY_END();
}
