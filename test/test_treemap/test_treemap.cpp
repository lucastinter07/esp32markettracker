// Runs on the PC, not the ESP32: pio test -e native
#include <unity.h>
#include <string.h>
#include "../../include/config.h"
#include "treemap.h"

void setUp() {}
void tearDown() {}

// The sector map's box on the screen (sector_screen.cpp uses the same numbers).
static const int16_t MAP_X = 4, MAP_Y = 56, MAP_W = 472, MAP_H = 240, GAP = 2;

static bool overlap(const TileRect& a, const TileRect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void test_config_weights_are_largest_first() {
  for (int i = 1; i < cfg::NUM_SECTORS; i++) {
    TEST_ASSERT_TRUE_MESSAGE(cfg::SECTOR_WEIGHTS[i - 1] >= cfg::SECTOR_WEIGHTS[i], cfg::SECTOR_SYMBOLS[i]);
  }
}

void test_tiles_cover_the_box_exactly_without_gaps() {
  TileRect r[cfg::NUM_SECTORS];
  squarify(cfg::SECTOR_WEIGHTS, cfg::NUM_SECTORS, 0, MAP_X, MAP_Y, MAP_W, MAP_H, 0, r);
  long area = 0;
  for (int i = 0; i < cfg::NUM_SECTORS; i++) {
    TEST_ASSERT_TRUE(r[i].w > 0 && r[i].h > 0);
    TEST_ASSERT_TRUE(r[i].x >= MAP_X && r[i].y >= MAP_Y);
    TEST_ASSERT_TRUE(r[i].x + r[i].w <= MAP_X + MAP_W && r[i].y + r[i].h <= MAP_Y + MAP_H);
    area += (long)r[i].w * r[i].h;
    for (int j = 0; j < i; j++) TEST_ASSERT_FALSE(overlap(r[i], r[j]));
  }
  TEST_ASSERT_EQUAL(MAP_W * MAP_H, area);   // no overlaps + full area = no holes
}

void test_tile_area_follows_weight() {
  TileRect r[cfg::NUM_SECTORS];
  squarify(cfg::SECTOR_WEIGHTS, cfg::NUM_SECTORS, 0, 0, 0, MAP_W, MAP_H, 0, r);
  float total = 0;
  for (int i = 0; i < cfg::NUM_SECTORS; i++) total += cfg::SECTOR_WEIGHTS[i];
  for (int i = 0; i < cfg::NUM_SECTORS; i++) {
    float expected = cfg::SECTOR_WEIGHTS[i] / total * MAP_W * MAP_H;
    float actual = (float)r[i].w * r[i].h;
    // Rounding to whole pixels moves each edge by at most half a pixel.
    float slack = (r[i].w + r[i].h) * 1.0f + 1;
    TEST_ASSERT_FLOAT_WITHIN(slack, expected, actual);
  }
}

void test_floor_makes_small_sectors_bigger() {
  TileRect plain[cfg::NUM_SECTORS], floored[cfg::NUM_SECTORS];
  squarify(cfg::SECTOR_WEIGHTS, cfg::NUM_SECTORS, 0, 0, 0, MAP_W, MAP_H, 0, plain);
  squarify(cfg::SECTOR_WEIGHTS, cfg::NUM_SECTORS, cfg::SECTOR_MIN_WEIGHT, 0, 0, MAP_W, MAP_H, 0, floored);
  int last = cfg::NUM_SECTORS - 1;
  TEST_ASSERT_TRUE(floored[last].w * floored[last].h > plain[last].w * plain[last].h);
}

void test_gap_separates_neighbours() {
  TileRect r[cfg::NUM_SECTORS];
  squarify(cfg::SECTOR_WEIGHTS, cfg::NUM_SECTORS, cfg::SECTOR_MIN_WEIGHT, MAP_X, MAP_Y, MAP_W, MAP_H, GAP, r);
  for (int i = 0; i < cfg::NUM_SECTORS; i++) {
    for (int j = 0; j < i; j++) TEST_ASSERT_FALSE(overlap(r[i], r[j]));
  }
}

// The promise behind the SIZED layout: with the real weights, the worst-case text fits
// in every tile ("XLRE", "-10.4%" / "-10%", a 7-character price, the longest name).
void test_worst_case_text_fits_every_tile() {
  TileRect r[cfg::NUM_SECTORS];
  squarify(cfg::SECTOR_WEIGHTS, cfg::NUM_SECTORS, cfg::SECTOR_MIN_WEIGHT, MAP_X, MAP_Y, MAP_W, MAP_H, GAP, r);
  for (int i = 0; i < cfg::NUM_SECTORS; i++) {
    int symLen = (int)strlen(cfg::SECTOR_SYMBOLS[i]);
    int nameLen = (int)strlen(cfg::SECTOR_NAMES[i]);
    TileText t = fitTile(r[i].w, r[i].h, symLen, nameLen, 7, 6, 4);
    int aw = r[i].w - 2 * t.pad;
    int pctLen = t.pctDecimals ? 6 : 4;
    TEST_ASSERT_TRUE_MESSAGE(pctLen * 6 * t.pctSize <= aw, cfg::SECTOR_SYMBOLS[i]);
    TEST_ASSERT_TRUE_MESSAGE(symLen * 6 * t.symSize <= aw, cfg::SECTOR_SYMBOLS[i]);
    TEST_ASSERT_TRUE_MESSAGE(tileTextHeight(t, t.showName) <= r[i].h, cfg::SECTOR_SYMBOLS[i]);
    if (t.showName) TEST_ASSERT_TRUE(nameLen * 6 <= aw);
    if (t.showPrice) TEST_ASSERT_TRUE(symLen * 6 * t.symSize + 6 + 7 * 6 <= aw);
  }
}

// The GRID layout's tiles (115 x 76) fit everything at full size, except a 6-character %.
void test_grid_tile_fits() {
  TileText t = fitTile(115, 76, 4, 13, 7, 6, 4);
  TEST_ASSERT_EQUAL(2, t.symSize);
  TEST_ASSERT_EQUAL(2, t.pctSize);   // "-10.4%" at size 3 is 108 px, too wide
  TEST_ASSERT_TRUE(t.showName);
  TEST_ASSERT_TRUE(t.showPrice);
  t = fitTile(115, 76, 3, 10, 6, 5, 3);   // "+1.8%"
  TEST_ASSERT_EQUAL(3, t.pctSize);
}

void test_tiny_tile_drops_the_decimal() {
  TileText t = fitTile(30, 40, 3, 6, 6, 6, 4);   // 24 px for text: "-10.4%" needs 36
  TEST_ASSERT_FALSE(t.pctDecimals);
  TEST_ASSERT_EQUAL(1, t.pctSize);
  TEST_ASSERT_FALSE(t.showName);
}

void test_heat_color_endpoints() {
  const uint16_t N = 0x2144, UP = 0x03C6, DOWN = 0xB123;
  TEST_ASSERT_EQUAL_HEX16(N, heatColor(0, 3, N, UP, DOWN));
  TEST_ASSERT_EQUAL_HEX16(UP, heatColor(3, 3, N, UP, DOWN));
  TEST_ASSERT_EQUAL_HEX16(UP, heatColor(8, 3, N, UP, DOWN));     // saturates
  TEST_ASSERT_EQUAL_HEX16(DOWN, heatColor(-3, 3, N, UP, DOWN));
  uint16_t half = heatColor(0.75f, 3, N, UP, DOWN);              // sqrt(0.25) = halfway
  TEST_ASSERT_TRUE(half != N && half != UP);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_config_weights_are_largest_first);
  RUN_TEST(test_tiles_cover_the_box_exactly_without_gaps);
  RUN_TEST(test_tile_area_follows_weight);
  RUN_TEST(test_floor_makes_small_sectors_bigger);
  RUN_TEST(test_gap_separates_neighbours);
  RUN_TEST(test_worst_case_text_fits_every_tile);
  RUN_TEST(test_grid_tile_fits);
  RUN_TEST(test_tiny_tile_drops_the_decimal);
  RUN_TEST(test_heat_color_endpoints);
  return UNITY_END();
}
