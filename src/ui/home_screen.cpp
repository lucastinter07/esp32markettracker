#include "ui/home_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "data/watchlist.h"
#include "hal/display.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "ui/gfx.h"
#include "ui/palette.h"
#include "ui/status_bar.h"

namespace home {

using ui::Align;

// Layout (480 x 320), as on the design canvas ("★ FINAL · Layout A · Graphite")
static const int16_t HEAD_Y = 32;                      // column headings, size 1
static const int16_t ROW_Y0 = 42, ROW_PITCH = 44, ROW_H = 42;
// Each column starts at a fixed x, with its heading exactly above it (left-aligned, so the
// headings line up with the text under them). Room per column at size 2: SYM 7 characters,
// LAST 7 ("1234.56"), CHG 6 ("+12.34"), % 7 ("+12.34%").
static const int16_t SYM_X = 8, LAST_X = 96, CHG_X = 188, PCT_X = 270;
static const int16_t SPARK_X = 362, SPARK_W = 110, SPARK_H = 26;
static const int16_t INDEX_RULE_Y = 264, INDEX_Y = 274, INDEX_X[2] = {8, 248};
static const int16_t HINT_Y = 300;

// What was last drawn, so update() can skip anything unchanged.
static uint32_t drawnStockVersion[cfg::NUM_TICKERS];
static uint32_t drawnIndexVersion[cfg::NUM_INDEXES];
static bool drawnStale[cfg::NUM_TICKERS];
static int drawnHighlight = -1;
static int32_t drawnDay = -1;              // the earnings mark ("E 3D") changes at midnight
static const char* drawnNotice = nullptr;  // string literals, so pointers can be compared

static bool marketOpen = false;   // checked once per drawFull()/update()

static bool rowStale(int i) {
  return isStale(stock(i), millis(), marketOpen);
}

static int32_t today() {
  EtTime now;
  return timeNowET(now) ? dayNumber(now.year, now.month, now.day) : -1;
}

// The notice in the bottom line: demo mode, a problem getting prices, prices that stopped,
// or no Wi-Fi. nullptr = all fine.
static const char* feedNotice(uint16_t& color) {
  const ui::Palette& p = ui::pal();
  color = p.target;
  if (cfg::DEMO_PRICES) return "DEMO PRICES";
  const char* problem = quoteFeedProblem();
  if (problem) {
    color = p.down;
    return problem;
  }
  if (wifiState() != WifiState::Connected) {
    color = p.down;
    return "WI-FI OFFLINE";
  }
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    if (rowStale(i)) return "PRICES DELAYED";
  }
  return nullptr;
}

// Today's chart as a 1 px line, 5 % headroom above and below.
static void drawSparkline(int16_t x, int16_t y, const PriceSeries& series, uint16_t color) {
  size_t n = series.size();
  if (n < 2) return;
  float lo = series.minValue(), hi = series.maxValue();
  float pad = (hi - lo) * 0.05f;
  if (pad < 0.0001f) pad = 0.0001f;
  lo -= pad;
  hi += pad;
  int16_t cols = n < (size_t)SPARK_W ? (int16_t)n : SPARK_W;
  int16_t prevX = 0, prevY = 0;
  for (int16_t k = 0; k < cols; k++) {
    // Each point (or, with more points than pixels, the average of a pixel column's points)
    size_t from = (size_t)k * n / cols, to = (size_t)(k + 1) * n / cols;
    if (to <= from) to = from + 1;
    float v = 0;
    for (size_t j = from; j < to; j++) v += series.at(j);
    v /= (float)(to - from);
    int16_t px = x + (int16_t)((int32_t)k * (SPARK_W - 1) / (cols - 1));
    int16_t py = y + (SPARK_H - 1) - (int16_t)((v - lo) / (hi - lo) * (SPARK_H - 1) + 0.5f);
    if (k > 0) ui::spanLine(prevX, prevY, px, py, color);
    prevX = px;
    prevY = py;
  }
}

static void drawRow(int i, bool highlighted) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  const SymbolState& s = stock(i);
  bool stale = rowStale(i);
  drawnStale[i] = stale;
  int16_t y = ROW_Y0 + i * ROW_PITCH;
  uint16_t bg = highlighted ? p.hi : p.bg;
  gfx.fillRect(0, y, cfg::SCREEN_W, ROW_H, bg);
  if (highlighted) gfx.fillRect(0, y, 3, ROW_H, p.stripe);
  gfx.drawFastHLine(0, y + ROW_H + 1, cfg::SCREEN_W, p.line);

  ui::text(SYM_X, y + 7, s.symbol, 2, p.label, bg);

  // Earnings within a week: "E 3D" / "E TODAY" under the symbol
  int32_t day = today();
  int daysAway = (s.earnings.valid && s.earnings.day != 0 && day >= 0) ? s.earnings.day - day : -1;
  if (daysAway >= 0 && daysAway <= 7) {
    char mark[12];
    if (daysAway == 0) snprintf(mark, sizeof(mark), "E TODAY");
    else snprintf(mark, sizeof(mark), "E %dD", daysAway);
    ui::text(SYM_X, y + 27, mark, 1, p.target, bg);
  }

  if (!s.hasData) {
    ui::text(LAST_X, y + 13, "WAITING...", 2, p.dim, bg);
    return;
  }
  bool up = s.quote.change >= 0;
  uint16_t color = up ? p.up : p.down;
  char buf[16];
  float price = s.quote.price, chg = s.quote.change;
  snprintf(buf, sizeof(buf), price >= 10000 ? "%.1f" : "%.2f", price);   // at most 7 characters
  ui::text(LAST_X, y + 13, buf, 2, stale ? p.dim : p.text, bg);   // grey = may be out of date
  snprintf(buf, sizeof(buf), fabsf(chg) >= 1000 ? "%+.0f" : (fabsf(chg) >= 100 ? "%+.1f" : "%+.2f"), chg);
  ui::text(CHG_X, y + 13, buf, 2, color, bg);
  snprintf(buf, sizeof(buf), "%+.2f%%", s.quote.changePct);
  ui::text(PCT_X, y + 13, buf, 2, color, bg);
  drawSparkline(SPARK_X, y + 8, s.intraday, color);
}

static void drawIndex(int k) {
  const ui::Palette& p = ui::pal();
  const SymbolState& s = indexEtf(k);
  int16_t x = INDEX_X[k];
  display().fillRect(x, INDEX_Y, 232, 16, p.bg);
  char buf[16];
  snprintf(buf, sizeof(buf), "%s ", s.symbol);
  x = ui::text(x, INDEX_Y, buf, 2, p.dim, p.bg);
  if (!s.hasData) {
    ui::text(x, INDEX_Y, "--", 2, p.dim, p.bg);
    return;
  }
  snprintf(buf, sizeof(buf), "%.2f ", s.quote.price);
  x = ui::text(x, INDEX_Y, buf, 2, p.text, p.bg);
  snprintf(buf, sizeof(buf), "%+.2f%%", s.quote.changePct);
  ui::text(x, INDEX_Y, buf, 2, s.quote.change >= 0 ? p.up : p.down, p.bg);
}

// The bottom line: what the index ETFs stand for, or a notice in its place.
static void drawHint() {
  const ui::Palette& p = ui::pal();
  uint16_t color = 0;
  const char* notice = feedNotice(color);
  display().fillRect(0, HINT_Y, 360, 8, p.bg);
  if (notice) {
    ui::text(8, HINT_Y, notice, 1, color, p.bg);
  } else {
    char what[48];
    snprintf(what, sizeof(what), "%s / %s PROXIES", watchlistIndexLabel(0), watchlistIndexLabel(1));
    ui::text(8, HINT_Y, what, 1, p.dim, p.bg);
  }
  drawnNotice = notice;
}

void drawFull(int highlightRow) {
  const ui::Palette& p = ui::pal();
  marketOpen = marketIsOpenNow();
  display().fillScreen(p.bg);
  statusBarDraw(true);
  ui::text(SYM_X, HEAD_Y, "SYM", 1, p.dim, p.bg);
  ui::text(LAST_X, HEAD_Y, "LAST", 1, p.dim, p.bg);
  ui::text(CHG_X, HEAD_Y, "CHG", 1, p.dim, p.bg);
  ui::text(PCT_X, HEAD_Y, "%", 1, p.dim, p.bg);
  ui::text(SPARK_X, HEAD_Y, "TODAY", 1, p.dim, p.bg);
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    drawRow(i, i == highlightRow);
    drawnStockVersion[i] = stock(i).version;
  }
  display().drawFastHLine(0, INDEX_RULE_Y, cfg::SCREEN_W, p.label);
  for (int k = 0; k < cfg::NUM_INDEXES; k++) {
    drawIndex(k);
    drawnIndexVersion[k] = indexEtf(k).version;
  }
  drawHint();
  ui::text(cfg::SCREEN_W - 6, HINT_Y, "TAP FOR SECTORS >", 1, p.label, p.bg, Align::Right);
  drawnHighlight = highlightRow;
  drawnDay = today();
}

void update(int highlightRow) {
  marketOpen = marketIsOpenNow();
  statusBarDraw(false);   // redraws itself only when its text changes
  int32_t day = today();
  bool newDay = day != drawnDay;
  drawnDay = day;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    bool highlightChanged = (i == highlightRow) != (i == drawnHighlight);
    bool staleChanged = rowStale(i) != drawnStale[i];
    if (newDay || highlightChanged || staleChanged || stock(i).version != drawnStockVersion[i]) {
      drawRow(i, i == highlightRow);
      drawnStockVersion[i] = stock(i).version;
    }
  }
  drawnHighlight = highlightRow;
  for (int k = 0; k < cfg::NUM_INDEXES; k++) {
    if (indexEtf(k).version != drawnIndexVersion[k]) {
      drawIndex(k);
      drawnIndexVersion[k] = indexEtf(k).version;
    }
  }
  uint16_t color;
  if (feedNotice(color) != drawnNotice) drawHint();
}

int rowAt(int16_t x, int16_t y) {
  (void)x;
  if (y < ROW_Y0 || y >= INDEX_RULE_Y) return -1;
  int row = (y - ROW_Y0) / ROW_PITCH;
  return row < cfg::NUM_TICKERS ? row : -1;
}

bool isMenuTap(int16_t x, int16_t y) {
  return x < 40 && y < STATUS_H + 4;   // a bit bigger than the icon, easier to hit
}

bool isIndexBarTap(int16_t x, int16_t y) {
  (void)x;
  return y >= INDEX_RULE_Y - 4;
}

}  // namespace home
