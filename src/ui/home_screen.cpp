#include "ui/home_screen.h"
#include <Arduino.h>
#include "config.h"
#include "data/market_data.h"
#include "hal/display.h"
#include "ui/chart.h"
#include "ui/status_bar.h"
#include "ui/theme.h"

namespace home {

// What was last drawn, so update() can skip anything unchanged.
static uint32_t drawnStockVersion[cfg::NUM_TICKERS];
static uint32_t drawnIndexVersion[cfg::NUM_INDEXES];
static int drawnHighlight = -1;

static void drawRow(int i, bool highlighted) {
  DFRobot_GDL& gfx = display();
  const SymbolState& s = stock(i);
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

  if (!s.hasData) {
    gfx.setTextColor(theme::DIM, bg);
    gfx.setCursor(100, ty);
    gfx.print("waiting...");
    return;
  }

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
  gfx.setTextColor(theme::DIM, theme::PANEL);
  gfx.setCursor(x + 10, y + 8);
  snprintf(buf, sizeof(buf), "%s (%s)", cfg::INDEX_LABELS[i], s.symbol);
  gfx.print(buf);
  if (cfg::DEMO_PRICES && i == cfg::NUM_INDEXES - 1) {
    // Real market status in the top bar + made-up prices would be misleading without this.
    gfx.setTextColor(theme::ACCENT, theme::PANEL);
    gfx.setCursor(x + w - 10 - 11 * 6, y + 8);
    gfx.print("DEMO PRICES");
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

  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    bool highlightChanged = (i == highlightRow) != (i == drawnHighlight);
    if (highlightChanged || stock(i).version != drawnStockVersion[i]) {
      drawRow(i, i == highlightRow);
      drawnStockVersion[i] = stock(i).version;
    }
  }
  drawnHighlight = highlightRow;

  for (int i = 0; i < cfg::NUM_INDEXES; i++) {
    if (indexEtf(i).version != drawnIndexVersion[i]) {
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
