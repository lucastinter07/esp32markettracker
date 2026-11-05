#include "ui/detail_screen.h"
#include <Arduino.h>
#include "config.h"
#include "data/market_data.h"
#include "hal/display.h"
#include "ui/chart.h"
#include "ui/theme.h"

namespace detail {

static const int16_t HEADER_H = 40;
static const int16_t BACK_W = 120;
static const int16_t CHART_X = 10, CHART_Y = 112, CHART_W = 300, CHART_H = 190;
static const int16_t STATS_X = 330, STATS_Y = 112;
static const int16_t BAR_Y = cfg::SCREEN_H - 6, BAR_H = 6;

static uint32_t drawnVersion = 0;
static int16_t drawnBarW = -1;

static void drawBadge(bool pinned) {
  DFRobot_GDL& gfx = display();
  int16_t x = cfg::SCREEN_W - 100;
  gfx.fillRect(x, 0, 100, HEADER_H, theme::PANEL);
  gfx.setTextSize(2);
  gfx.setTextColor(pinned ? theme::ACCENT : theme::DIM, theme::PANEL);
  gfx.setCursor(x + 10, 12);
  gfx.print(pinned ? "PINNED" : "AUTO");
}

static void drawHeader(int ticker, bool pinned) {
  DFRobot_GDL& gfx = display();
  gfx.fillRect(0, 0, cfg::SCREEN_W, HEADER_H, theme::PANEL);
  gfx.fillRect(0, 0, BACK_W, HEADER_H, theme::HIGHLIGHT);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::TEXT, theme::HIGHLIGHT);
  gfx.setCursor(12, 12);
  gfx.print("< BACK");

  gfx.setTextSize(3);
  gfx.setTextColor(theme::TEXT, theme::PANEL);
  gfx.setCursor(BACK_W + 20, 9);
  gfx.print(stock(ticker).symbol);
  drawBadge(pinned);
}

static void drawStat(int row, const char* label, float value) {
  DFRobot_GDL& gfx = display();
  int16_t y = STATS_Y + row * 32;
  char buf[16];
  gfx.setTextSize(1);
  gfx.setTextColor(theme::DIM, theme::BG);
  gfx.setCursor(STATS_X, y);
  gfx.print(label);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::TEXT, theme::BG);
  gfx.setCursor(STATS_X, y + 11);
  snprintf(buf, sizeof(buf), "%9.2f", value);
  gfx.print(buf);
}

static void drawData(int ticker) {
  DFRobot_GDL& gfx = display();
  const SymbolState& s = stock(ticker);
  const Quote& q = s.quote;
  uint16_t chg = theme::changeColor(q.change);
  char buf[32];

  // Big price + change line
  gfx.setTextSize(4);
  gfx.setTextColor(theme::TEXT, theme::BG);
  gfx.setCursor(10, 52);
  snprintf(buf, sizeof(buf), "%8.2f", q.price);
  gfx.print(buf);
  gfx.setTextSize(2);
  gfx.setTextColor(chg, theme::BG);
  gfx.setCursor(220, 66);
  snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%)   ", q.change, q.changePct);
  gfx.print(buf);

  drawLineChart(CHART_X, CHART_Y, CHART_W, CHART_H, s.intraday, chg, theme::BG, true, q.prevClose);

  drawStat(0, "OPEN", q.open);
  drawStat(1, "PREV CLOSE", q.prevClose);
  drawStat(2, "DAY HIGH", q.high);
  drawStat(3, "DAY LOW", q.low);
  // 52-week high/low, P/E etc. come from /stock/metric once Finnhub is connected.
}

void drawFull(int ticker, bool pinned) {
  display().fillScreen(theme::BG);
  drawHeader(ticker, pinned);
  drawData(ticker);
  drawnVersion = stock(ticker).version;
  drawnBarW = -1;
}

void update(int ticker) {
  if (stock(ticker).version == drawnVersion) return;
  drawData(ticker);
  drawnVersion = stock(ticker).version;
}

void drawCountdown(float remaining) {
  if (remaining < 0) remaining = 0;
  int16_t w = (int16_t)(remaining * cfg::SCREEN_W);
  if (w == drawnBarW) return;   // only touch the screen when a pixel actually changes
  DFRobot_GDL& gfx = display();
  gfx.fillRect(0, BAR_Y, w, BAR_H, theme::ACCENT);
  gfx.fillRect(w, BAR_Y, cfg::SCREEN_W - w, BAR_H, theme::BG);
  drawnBarW = w;
}

void setPinned(bool pinned) {
  drawBadge(pinned);
  if (pinned) {
    display().fillRect(0, BAR_Y, cfg::SCREEN_W, BAR_H, theme::BG);   // no countdown when pinned
    drawnBarW = 0;
  }
}

bool isBackTap(int16_t x, int16_t y) {
  // Generous target: a fingertip is ~40 px wide, so accept taps a bit beyond the box.
  return x < BACK_W + 40 && y < HEADER_H + 20;
}

}  // namespace detail
