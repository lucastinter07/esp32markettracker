#include "ui/home_screen.h"
#include <Arduino.h>
#include "config.h"
#include "data/market_data.h"
#include "hal/display.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "ui/chart.h"
#include "ui/status_bar.h"
#include "ui/theme.h"

namespace home {

// What was last drawn, so update() can skip anything unchanged.
static uint32_t drawnStockVersion[cfg::NUM_TICKERS];
static uint32_t drawnIndexVersion[cfg::NUM_INDEXES];
static int drawnHighlight = -1;
static const char* drawnLabel = nullptr;
static bool drawnStale[cfg::NUM_TICKERS];

// Checked once at the start of each drawFull()/update() and reused for every row.
static bool marketOpen = false;

static bool rowStale(int i) {
  return isStale(stock(i), millis(), marketOpen);
}

// Small notice in the index bar: demo mode, a problem getting live prices, or prices
// that have stopped updating. Returns string literals, so the same notice always has
// the same address and update() can compare pointers to spot a change.
static const char* feedLabel() {
  if (cfg::DEMO_PRICES) return "DEMO PRICES";
  const char* problem = quoteFeedProblem();
  if (problem) return problem;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    if (rowStale(i)) return "PRICES DELAYED";
  }
  return nullptr;   // live prices are fine
}

static uint16_t feedLabelColor(const char* label) {
  if (cfg::DEMO_PRICES || strcmp(label, "PRICES DELAYED") == 0) return theme::ACCENT;
  return theme::DOWN;   // a real problem
}

static void drawRow(int i, bool highlighted) {
  DFRobot_GDL& gfx = display();
  const SymbolState& s = stock(i);
  bool stale = rowStale(i);
  drawnStale[i] = stale;
  int16_t y = theme::ROW_Y0 + i * theme::ROW_H;
  uint16_t bg = highlighted ? theme::HIGHLIGHT : theme::BG;
  uint16_t chg = theme::changeColor(s.quote.change);
  char buf[16];

  gfx.fillRect(0, y, cfg::SCREEN_W, theme::ROW_H - 2, bg);
  gfx.setTextSize(2);
  int16_t ty = y + (theme::ROW_H - 16) / 2;

  gfx.setTextColor(theme::TEXT, bg);
  gfx.setCursor(10, ty);
  gfx.print(s.symbol);

  // Earnings within a week: small amber marker under the symbol, e.g. "E 3d"
  EtTime now;
  if (s.earnings.valid && s.earnings.day != 0 && timeNowET(now)) {
    int daysAway = s.earnings.day - dayNumber(now.year, now.month, now.day);
    if (daysAway >= 0 && daysAway <= 7) {
      char mark[10];
      if (daysAway == 0) snprintf(mark, sizeof(mark), "E today");
      else snprintf(mark, sizeof(mark), "E %dd", daysAway);
      gfx.setTextSize(1);
      gfx.setTextColor(theme::ACCENT, bg);
      gfx.setCursor(10, y + theme::ROW_H - 11);
      gfx.print(mark);
      gfx.setTextSize(2);
    }
  }

  if (!s.hasData) {
    gfx.setTextColor(theme::DIM, bg);
    gfx.setCursor(100, ty);
    gfx.print("waiting...");
    return;
  }

  // A stale price is drawn grey: it may no longer be right.
  gfx.setTextColor(stale ? theme::DIM : theme::TEXT, bg);
  snprintf(buf, sizeof(buf), "%7.2f", s.quote.price);
  gfx.setCursor(84, ty);
  gfx.print(buf);

  gfx.setTextColor(chg, bg);
  snprintf(buf, sizeof(buf), "%+6.2f", s.quote.change);
  gfx.setCursor(180, ty);
  gfx.print(buf);
  snprintf(buf, sizeof(buf), "%+5.2f%%", s.quote.changePct);
  gfx.setCursor(270, ty);
  gfx.print(buf);

  drawLineChart(370, y + 6, 100, theme::ROW_H - 14, s.intraday, chg, bg);
}

static void drawIndex(int i) {
  DFRobot_GDL& gfx = display();
  const SymbolState& s = indexEtf(i);
  int16_t w = cfg::SCREEN_W / cfg::NUM_INDEXES;
  int16_t x = i * w;
  int16_t y = theme::FOOTER_Y;
  int16_t h = cfg::SCREEN_H - y;
  char buf[32];

  gfx.fillRect(x + 2, y, w - 4, h, theme::PANEL);
  gfx.setTextSize(1);
  gfx.setTextColor(theme::LABEL, theme::PANEL);
  gfx.setCursor(x + 10, y + 8);
  snprintf(buf, sizeof(buf), "%s (%s)", cfg::INDEX_LABELS[i], s.symbol);
  gfx.print(buf);
  if (i == cfg::NUM_INDEXES - 1) {
    const char* label = feedLabel();
    if (label) {
      gfx.setTextColor(feedLabelColor(label), theme::PANEL);
      gfx.setCursor(x + w - 10 - strlen(label) * 6, y + 8);   // right-aligned
      gfx.print(label);
    }
    drawnLabel = label;
  }

  if (!s.hasData) return;
  gfx.setTextSize(2);
  gfx.setTextColor(theme::TEXT, theme::PANEL);
  gfx.setCursor(x + 10, y + 26);
  snprintf(buf, sizeof(buf), "%.2f", s.quote.price);
  gfx.print(buf);
  gfx.setTextColor(theme::changeColor(s.quote.change), theme::PANEL);
  gfx.setCursor(x + 120, y + 26);
  snprintf(buf, sizeof(buf), "%+.2f%%", s.quote.changePct);
  gfx.print(buf);
}

void drawFull(int highlightRow) {
  marketOpen = marketIsOpenNow();
  display().fillScreen(theme::BG);
  statusBarDraw(true);
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    drawRow(i, i == highlightRow);
    drawnStockVersion[i] = stock(i).version;
  }
  for (int i = 0; i < cfg::NUM_INDEXES; i++) {
    drawIndex(i);
    drawnIndexVersion[i] = indexEtf(i).version;
  }
  drawnHighlight = highlightRow;
}

void update(int highlightRow) {
  statusBarDraw(false);   // redraws itself only when its text changes
  marketOpen = marketIsOpenNow();

  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    bool highlightChanged = (i == highlightRow) != (i == drawnHighlight);
    bool staleChanged = rowStale(i) != drawnStale[i];
    if (highlightChanged || staleChanged || stock(i).version != drawnStockVersion[i]) {
      drawRow(i, i == highlightRow);
      drawnStockVersion[i] = stock(i).version;
    }
  }
  drawnHighlight = highlightRow;

  for (int i = 0; i < cfg::NUM_INDEXES; i++) {
    bool labelChanged = i == cfg::NUM_INDEXES - 1 && feedLabel() != drawnLabel;
    if (labelChanged || indexEtf(i).version != drawnIndexVersion[i]) {
      drawIndex(i);
      drawnIndexVersion[i] = indexEtf(i).version;
    }
  }
}

int rowAt(int16_t x, int16_t y) {
  (void)x;
  if (y < theme::ROW_Y0) return -1;
  int row = (y - theme::ROW_Y0) / theme::ROW_H;
  return row < cfg::NUM_TICKERS ? row : -1;
}

}  // namespace home
