#include "ui/sector_screen.h"
#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "data/sectors.h"
#include "data/settings.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "treemap.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace sectorsui {

using ui::Align;

// Layout (px). The map box is the one test_treemap checks the text fitting against.
static const int16_t MAP_X = 4, MAP_Y = 32, MAP_W = 472, MAP_H = 260, MAP_GAP = 2;
static const int16_t GRID_COLS = 4, GRID_W = 115, GRID_H = 84, GRID_GAP = 4;   // 4 x 115 + 3 x 4 = 472
static const int16_t FOOTER_Y = 302;

static const char* const DAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static TileRect sizedRects[cfg::NUM_SECTORS];   // worked out once: the weights don't change
static bool sizedReady = false;

// What's on screen, so update() redraws only what changed.
static uint8_t drawnLayout = 0xFF;
static int8_t drawnSlot[NUM_SECTOR_QUOTES];       // which sector each tile slot shows
static uint32_t drawnVersion[NUM_SECTOR_QUOTES];  // that sector's version when drawn
static char drawnHeader[64];
static char drawnFooter[32];

static bool gridLayout() {
  return settings().sectorMap == SECTOR_MAP_GRID;
}

// GRID has a 12th tile for SPY; SIZED shows SPY only in the header.
static int slotCount() {
  return gridLayout() ? NUM_SECTOR_QUOTES : cfg::NUM_SECTORS;
}

static TileRect slotRect(int slot) {
  if (!gridLayout()) {
    if (!sizedReady) {
      squarify(cfg::SECTOR_WEIGHTS, cfg::NUM_SECTORS, cfg::SECTOR_MIN_WEIGHT, MAP_X, MAP_Y, MAP_W, MAP_H,
               MAP_GAP, sizedRects);
      sizedReady = true;
    }
    return sizedRects[slot];
  }
  TileRect r;
  r.x = MAP_X + (slot % GRID_COLS) * (GRID_W + GRID_GAP);
  r.y = MAP_Y + (slot / GRID_COLS) * (GRID_H + GRID_GAP);
  r.w = GRID_W;
  r.h = GRID_H;
  return r;
}

// GRID order: higher change first; sectors without a price yet go last.
static bool comesBefore(int a, int b) {
  const SectorQuote& qa = sectorQuote(a);
  const SectorQuote& qb = sectorQuote(b);
  if (qa.valid != qb.valid) return qa.valid;
  return qa.valid && qa.changePct > qb.changePct;
}

// Which sector each slot shows. SIZED: fixed (config order, largest weight first).
// GRID: sorted by today's change (ties keep config order), SPY in the last slot.
static void computeOrder(int8_t* order) {
  for (int k = 0; k < NUM_SECTOR_QUOTES; k++) order[k] = (int8_t)k;
  if (!gridLayout()) return;
  for (int i = 1; i < cfg::NUM_SECTORS; i++) {   // insertion sort: 11 items
    int8_t v = order[i];
    int j = i - 1;
    while (j >= 0 && comesBefore(v, order[j])) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = v;
  }
}

static void formatPct(char* buf, size_t size, float pct, int decimals) {
  if (fabsf(pct) < 0.05f) pct = 0;   // no "-0.0%"
  snprintf(buf, size, "%+.*f%%", decimals, pct);
}

static uint16_t heat(float pct) {
  const ui::Palette& p = ui::pal();
  return heatColor(pct, cfg::SECTOR_SATURATE_PCT, p.heatNeutral, p.heatUp, p.heatDown);
}

// One tile. Text sizes come from fitTile(), so nothing ever runs past the tile's edge.
static void drawTile(const TileRect& r, int k) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  const SectorQuote& q = sectorQuote(k);
  bool benchmark = k == BENCHMARK_INDEX;
  uint16_t bg = benchmark ? p.bg : (q.valid ? heat(q.changePct) : p.heatNeutral);
  gfx.fillRect(r.x, r.y, r.w, r.h, bg);
  if (benchmark) gfx.drawRect(r.x, r.y, r.w, r.h, p.edge);
  uint16_t soft = ui::mix(bg, p.text, 190);   // name and price: a little quieter than the symbol

  const char* sym = sectorSymbol(k);
  const char* name = benchmark ? "S&P 500" : cfg::SECTOR_NAMES[k];
  char upperName[24];
  snprintf(upperName, sizeof(upperName), "%s", name);
  for (char* c = upperName; *c; c++) *c = (char)toupper((unsigned char)*c);
  char price[12] = "";
  char pct1[12] = "--";
  char pct0[12] = "--";
  if (q.valid) {
    snprintf(price, sizeof(price), "%.2f", q.price);
    formatPct(pct1, sizeof(pct1), q.changePct, 1);
    formatPct(pct0, sizeof(pct0), q.changePct, 0);
  }
  TileText t = fitTile(r.w, r.h, (int)strlen(sym), (int)strlen(upperName), (int)strlen(price), (int)strlen(pct1),
                       (int)strlen(pct0));
  int16_t x = r.x + t.pad;
  int16_t top = r.y + t.pad;
  ui::text(x, top, sym, t.symSize, p.text, bg);
  if (t.showPrice && price[0]) {   // right-aligned, on the symbol's baseline
    ui::text(r.x + r.w - t.pad, top + t.symSize * 8 - 8, price, 1, soft, bg, Align::Right);
  }
  if (t.showName) ui::text(x, top + t.symSize * 8 + 5, upperName, 1, soft, bg);
  uint16_t pctColor = benchmark && q.valid ? (q.change >= 0 ? p.up : p.down) : p.text;
  ui::text(x, r.y + r.h - t.pad - t.pctSize * 8, t.pctDecimals ? pct1 : pct0, t.pctSize, pctColor, bg);
}

// Header bar: "< SECTORS . 6(up) 5(down)" on the left, "SPY +0.41%" on the right.
static void drawHeader(bool force) {
  int up = 0, down = 0;
  for (int k = 0; k < cfg::NUM_SECTORS; k++) {
    const SectorQuote& q = sectorQuote(k);
    if (!q.valid) continue;
    if (q.changePct >= 0) up++;
    else down++;
  }
  char left[40], right[24] = "";
  if (up + down > 0) snprintf(left, sizeof(left), "< SECTORS " G_DOT " %d" G_UP " %d" G_DOWN, up, down);
  else snprintf(left, sizeof(left), "< SECTORS");
  const SectorQuote& spy = sectorQuote(BENCHMARK_INDEX);
  if (spy.valid) {
    char pct[12];
    formatPct(pct, sizeof(pct), spy.changePct, 2);
    snprintf(right, sizeof(right), "%s %s", sectorSymbol(BENCHMARK_INDEX), pct);
  }
  char key[sizeof(drawnHeader)];
  snprintf(key, sizeof(key), "%s|%s", left, right);
  if (!force && strcmp(key, drawnHeader) == 0) return;
  strcpy(drawnHeader, key);
  ui::headerBar(left, right);
}

// Bottom left: the color scale (and, for SIZED, what the tile size means). Static.
static void drawLegend() {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  char buf[32];
  snprintf(buf, sizeof(buf), "-%.0f%% ", cfg::SECTOR_SATURATE_PCT);
  int16_t x = ui::text(6, FOOTER_Y, buf, 1, p.dim, p.bg);
  for (int i = 0; i < 7; i++) gfx.fillRect(x + i * 11, FOOTER_Y, 10, 8, heat(cfg::SECTOR_SATURATE_PCT * (i - 3) / 3));
  snprintf(buf, sizeof(buf), "+%.0f%%%s", cfg::SECTOR_SATURATE_PCT, gridLayout() ? "" : "  SIZE = S&P WEIGHT");
  ui::text(x + 80, FOOTER_Y, buf, 1, p.dim, p.bg);
}

// "UPDATED 2 MIN AGO" while open, "AS OF WED CLOSE" while closed.
static void footerText(char* buf, size_t size) {
  uint32_t newest = sectorsNewestFetch();
  EtTime now;
  if (newest == 0 || !timeNowET(now)) {
    snprintf(buf, size, "WAITING FOR PRICES");
    return;
  }
  if (marketStatus(now).phase == MarketPhase::Open) {
    uint32_t t = (uint32_t)time(nullptr);
    unsigned long minutes = t > newest ? (t - newest) / 60 : 0;
    if (minutes == 0) snprintf(buf, size, "UPDATED JUST NOW");
    else snprintf(buf, size, "UPDATED %lu MIN AGO", minutes);
  } else {
    int y, m, d;
    civilFromDay(lastSessionDay(now), y, m, d);
    snprintf(buf, size, "AS OF %s CLOSE", DAY_NAMES[weekdayOf(y, m, d)]);
  }
}

static void drawFooter(bool force) {
  char text[sizeof(drawnFooter)];
  footerText(text, sizeof(text));
  if (!force && strcmp(text, drawnFooter) == 0) return;
  strcpy(drawnFooter, text);
  const ui::Palette& p = ui::pal();
  display().fillRect(300, FOOTER_Y, cfg::SCREEN_W - 300, 8, p.bg);
  ui::text(cfg::SCREEN_W - 6, FOOTER_Y, text, 1, p.dim, p.bg, Align::Right);
}

void drawFull() {
  display().fillScreen(ui::pal().bg);
  drawHeader(true);
  drawLegend();
  drawnLayout = settings().sectorMap;
  int8_t order[NUM_SECTOR_QUOTES];
  computeOrder(order);
  for (int slot = 0; slot < slotCount(); slot++) {
    drawTile(slotRect(slot), order[slot]);
    drawnSlot[slot] = order[slot];
    drawnVersion[slot] = sectorsVersion(order[slot]);
  }
  drawFooter(true);
}

void update() {
  if (settings().sectorMap != drawnLayout) {
    drawFull();
    return;
  }
  int8_t order[NUM_SECTOR_QUOTES];
  computeOrder(order);
  for (int slot = 0; slot < slotCount(); slot++) {
    int k = order[slot];
    if (k != drawnSlot[slot] || sectorsVersion(k) != drawnVersion[slot]) {
      drawTile(slotRect(slot), k);
      drawnSlot[slot] = (int8_t)k;
      drawnVersion[slot] = sectorsVersion(k);
    }
  }
  drawHeader(false);
  drawFooter(false);
}

bool isBackTap(int16_t x, int16_t y) {
  return x < 140 && y < ui::BAR_H + 12;
}

}  // namespace sectorsui
