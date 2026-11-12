#include "ui/sector_screen.h"
#include <Arduino.h>
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
#include "ui/status_bar.h"
#include "ui/theme.h"

namespace sectorsui {

// Layout (px). The map box is the one test_treemap checks the text fitting against.
static const int16_t MAP_X = 4, MAP_Y = 56, MAP_W = 472, MAP_H = 240, MAP_GAP = 2;
static const int16_t GRID_COLS = 4, GRID_W = 115, GRID_H = 76, GRID_GAP = 4;   // 4 x 115 + 3 x 4 = 472
static const int16_t TITLE_Y = 36;
static const int16_t HEADER_INFO_X = 100;   // up/down counts and SPY, right of the title
static const int16_t FOOTER_Y = 304;
static const int16_t FOOTER_TEXT_X = 270;   // the data age, right-aligned in 270..472

static const char* const DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static TileRect sizedRects[cfg::NUM_SECTORS];   // worked out once: the weights don't change
static bool sizedReady = false;

// What's on screen, so update() redraws only what changed.
static uint8_t drawnLayout = 0xFF;
static int8_t drawnSlot[NUM_SECTOR_QUOTES];       // which sector each tile slot shows
static uint32_t drawnVersion[NUM_SECTOR_QUOTES];  // that sector's version when drawn
static char drawnHeader[40];
static char drawnFooter[32];

static bool gridLayout() {
  return settings().sectorMap == SECTOR_MAP_GRID;
}

// GRID has a 12th tile for SPY; SIZED shows SPY in the header instead.
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

// One tile. Text sizes come from fitTile(), so nothing ever runs past the tile's edge.
static void drawTile(const TileRect& r, int k) {
  DFRobot_GDL& gfx = display();
  const SectorQuote& q = sectorQuote(k);
  bool benchmark = k == BENCHMARK_INDEX;
  uint16_t bg = benchmark ? theme::BG
                : q.valid ? heatColor(q.changePct, cfg::SECTOR_SATURATE_PCT, theme::HEAT_NEUTRAL, theme::HEAT_UP,
                                      theme::HEAT_DOWN)
                          : theme::HEAT_NEUTRAL;
  gfx.fillRect(r.x, r.y, r.w, r.h, bg);
  if (benchmark) gfx.drawRect(r.x, r.y, r.w, r.h, theme::FRAME);

  const char* sym = sectorSymbol(k);
  const char* name = benchmark ? "S&P 500" : cfg::SECTOR_NAMES[k];
  char price[12] = "";
  char pct1[12] = "--";
  char pct0[12] = "--";
  if (q.valid) {
    snprintf(price, sizeof(price), "%.2f", q.price);
    formatPct(pct1, sizeof(pct1), q.changePct, 1);
    formatPct(pct0, sizeof(pct0), q.changePct, 0);
  }
  TileText t = fitTile(r.w, r.h, (int)strlen(sym), (int)strlen(name), (int)strlen(price), (int)strlen(pct1),
                       (int)strlen(pct0));
  int16_t x = r.x + t.pad;
  int16_t top = r.y + t.pad;

  gfx.setTextSize(t.symSize);
  gfx.setTextColor(theme::TEXT, bg);
  gfx.setCursor(x, top);
  gfx.print(sym);

  gfx.setTextSize(1);
  gfx.setTextColor(theme::LABEL, bg);
  if (t.showPrice && price[0]) {   // right-aligned, on the symbol's baseline
    gfx.setCursor(r.x + r.w - t.pad - (int16_t)strlen(price) * 6, top + t.symSize * 8 - 8);
    gfx.print(price);
  }
  if (t.showName) {
    gfx.setCursor(x, top + t.symSize * 8 + 5);
    gfx.print(name);
  }

  gfx.setTextSize(t.pctSize);
  gfx.setTextColor(benchmark && q.valid ? theme::changeColor(q.change) : theme::TEXT, bg);
  gfx.setCursor(x, r.y + r.h - t.pad - t.pctSize * 8);
  gfx.print(t.pctDecimals ? pct1 : pct0);
}

// Right of the title: "6 up  5 down", and (SIZED) SPY's change against the right edge.
static void drawHeaderInfo(bool force) {
  int up = 0, down = 0;
  for (int k = 0; k < cfg::NUM_SECTORS; k++) {
    const SectorQuote& q = sectorQuote(k);
    if (!q.valid) continue;
    if (q.changePct >= 0) up++;
    else down++;
  }
  const SectorQuote& spy = sectorQuote(BENCHMARK_INDEX);
  char spyText[12] = "";
  if (!gridLayout() && spy.valid) formatPct(spyText, sizeof(spyText), spy.changePct, 2);
  char key[sizeof(drawnHeader)];
  snprintf(key, sizeof(key), "%d|%d|%s", up, down, spyText);
  if (!force && strcmp(key, drawnHeader) == 0) return;
  strcpy(drawnHeader, key);

  DFRobot_GDL& gfx = display();
  gfx.fillRect(HEADER_INFO_X, theme::HEADER_H + 2, cfg::SCREEN_W - HEADER_INFO_X, 22, theme::BG);
  gfx.setTextSize(1);
  if (up + down > 0) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d up", up);
    gfx.setTextColor(theme::UP, theme::BG);
    gfx.setCursor(HEADER_INFO_X, TITLE_Y + 4);
    gfx.print(buf);
    int16_t x = HEADER_INFO_X + (int16_t)strlen(buf) * 6 + 12;
    snprintf(buf, sizeof(buf), "%d down", down);
    gfx.setTextColor(theme::DOWN, theme::BG);
    gfx.setCursor(x, TITLE_Y + 4);
    gfx.print(buf);
  }
  if (spyText[0]) {
    int16_t pctX = cfg::SCREEN_W - 8 - (int16_t)strlen(spyText) * 12;
    gfx.setTextSize(2);
    gfx.setTextColor(theme::changeColor(spy.change), theme::BG);
    gfx.setCursor(pctX, TITLE_Y);
    gfx.print(spyText);
    static const char* const SPY_LABEL = "S&P 500 (SPY)";
    gfx.setTextSize(1);
    gfx.setTextColor(theme::LABEL, theme::BG);
    gfx.setCursor(pctX - 6 - (int16_t)strlen(SPY_LABEL) * 6, TITLE_Y + 4);
    gfx.print(SPY_LABEL);
  }
}

// Bottom left: the color scale (and, for SIZED, what the tile size means). Static.
static void drawLegend() {
  DFRobot_GDL& gfx = display();
  char buf[8];
  gfx.setTextSize(1);
  gfx.setTextColor(theme::LABEL, theme::BG);
  snprintf(buf, sizeof(buf), "-%.0f%%", cfg::SECTOR_SATURATE_PCT);
  gfx.setCursor(8, FOOTER_Y);
  gfx.print(buf);
  int16_t x = 8 + (int16_t)strlen(buf) * 6 + 4;
  for (int i = -3; i <= 3; i++) {
    float pct = cfg::SECTOR_SATURATE_PCT * i / 3;
    uint16_t c = heatColor(pct, cfg::SECTOR_SATURATE_PCT, theme::HEAT_NEUTRAL, theme::HEAT_UP, theme::HEAT_DOWN);
    gfx.fillRect(x, FOOTER_Y, 14, 8, c);
    x += 15;
  }
  x += 3;
  snprintf(buf, sizeof(buf), "+%.0f%%", cfg::SECTOR_SATURATE_PCT);
  gfx.setCursor(x, FOOTER_Y);
  gfx.print(buf);
  if (!gridLayout()) {
    gfx.setCursor(x + (int16_t)strlen(buf) * 6 + 10, FOOTER_Y);
    gfx.print("size = S&P weight");
  }
}

// "updated 2 min ago" while open, "as of Wed close" while closed.
static void footerText(char* buf, size_t size) {
  uint32_t newest = sectorsNewestFetch();
  EtTime now;
  if (newest == 0 || !timeNowET(now)) {
    snprintf(buf, size, "waiting for prices");
    return;
  }
  if (marketStatus(now).phase == MarketPhase::Open) {
    uint32_t t = (uint32_t)time(nullptr);
    unsigned long minutes = t > newest ? (t - newest) / 60 : 0;
    if (minutes == 0) snprintf(buf, size, "updated just now");
    else snprintf(buf, size, "updated %lu min ago", minutes);
  } else {
    int y, m, d;
    civilFromDay(lastSessionDay(now), y, m, d);
    snprintf(buf, size, "as of %s close", DAY_NAMES[weekdayOf(y, m, d)]);
  }
}

static void drawFooter(bool force) {
  char text[sizeof(drawnFooter)];
  footerText(text, sizeof(text));
  if (!force && strcmp(text, drawnFooter) == 0) return;
  strcpy(drawnFooter, text);
  DFRobot_GDL& gfx = display();
  gfx.fillRect(FOOTER_TEXT_X, FOOTER_Y - 2, cfg::SCREEN_W - FOOTER_TEXT_X, 12, theme::BG);
  gfx.setTextSize(1);
  gfx.setTextColor(theme::LABEL, theme::BG);
  gfx.setCursor(cfg::SCREEN_W - 8 - (int16_t)strlen(text) * 6, FOOTER_Y);
  gfx.print(text);
}

void drawFull() {
  DFRobot_GDL& gfx = display();
  gfx.fillScreen(theme::BG);
  statusBarDraw(true);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::ACCENT, theme::BG);
  gfx.setCursor(8, TITLE_Y);
  gfx.print("SECTORS");
  drawLegend();

  drawnLayout = settings().sectorMap;
  int8_t order[NUM_SECTOR_QUOTES];
  computeOrder(order);
  for (int slot = 0; slot < slotCount(); slot++) {
    drawTile(slotRect(slot), order[slot]);
    drawnSlot[slot] = order[slot];
    drawnVersion[slot] = sectorsVersion(order[slot]);
  }
  drawHeaderInfo(true);
  drawFooter(true);
}

void update() {
  statusBarDraw(false);   // redraws itself only when its text changes
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
  drawHeaderInfo(false);
  drawFooter(false);
}

}  // namespace sectorsui
