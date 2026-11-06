#include "ui/detail_screen.h"
#include <Arduino.h>
#include "config.h"
#include "data/market_data.h"
#include "hal/display.h"
#include "net/time_sync.h"
#include "ui/chart.h"
#include "ui/theme.h"

namespace detail {

static const int16_t HEADER_H = 40;
static const int16_t BACK_W = 120;
static const int16_t CHART_X = 10, CHART_Y = 112, CHART_W = 270, CHART_H = 190;
// Stats: two columns of four, 90 px wide and 48 px apart
static const int16_t STATS_X[2] = {296, 386};
static const int16_t STATS_Y = 112, STATS_ROW_H = 48;
static const int16_t BAR_Y = cfg::SCREEN_H - 6, BAR_H = 6;

static uint32_t drawnVersion = 0;
static int drawnStaleMin = -1;   // minutes since the last quote when stale, -1 when fresh
static int16_t drawnBarW = -1;

// How many whole minutes old the quote is when it counts as stale, otherwise -1.
static int staleMinutes(const SymbolState& s) {
  uint32_t nowMs = millis();
  if (!isStale(s, nowMs, marketIsOpenNow())) return -1;
  return (nowMs - s.lastUpdateMs) / 60000;
}

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

// Prices in at most 7 characters, so a value never runs into the next column.
static void formatPrice(char* buf, size_t size, float v) {
  if (v >= 10000) snprintf(buf, size, "%.0f", v);
  else if (v >= 1000) snprintf(buf, size, "%.1f", v);
  else snprintf(buf, size, "%.2f", v);
}

// 4.82T, 512.3B, 980M
static void formatMarketCap(char* buf, size_t size, double dollars) {
  if (dollars >= 1e12) snprintf(buf, size, "%.2fT", dollars / 1e12);
  else if (dollars >= 1e9) snprintf(buf, size, "%.1fB", dollars / 1e9);
  else snprintf(buf, size, "%.0fM", dollars / 1e6);
}

// One stat: small grey label, larger value under it. "--" when there's no value yet.
static void drawStat(int col, int row, const char* label, const char* value) {
  DFRobot_GDL& gfx = display();
  int16_t x = STATS_X[col];
  int16_t y = STATS_Y + row * STATS_ROW_H;
  char padded[12];
  snprintf(padded, sizeof(padded), "%-7s", value[0] ? value : "--");   // spaces cover old text
  gfx.setTextSize(1);
  gfx.setTextColor(theme::DIM, theme::BG);
  gfx.setCursor(x, y);
  gfx.print(label);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::TEXT, theme::BG);
  gfx.setCursor(x, y + 11);
  gfx.print(padded);
}

static void drawPriceStat(int col, int row, const char* label, float v) {
  char buf[12] = "";
  if (v > 0) formatPrice(buf, sizeof(buf), v);
  drawStat(col, row, label, buf);
}

static void drawData(int ticker) {
  DFRobot_GDL& gfx = display();
  const SymbolState& s = stock(ticker);
  const Quote& q = s.quote;
  const Fundamentals& f = s.fundamentals;
  uint16_t chg = theme::changeColor(q.change);
  int staleMin = staleMinutes(s);
  char buf[32];

  // Big price (grey when stale) + change line
  gfx.setTextSize(4);
  gfx.setTextColor(staleMin >= 0 ? theme::DIM : theme::TEXT, theme::BG);
  gfx.setCursor(10, 52);
  snprintf(buf, sizeof(buf), "%8.2f", q.price);
  gfx.print(buf);
  gfx.setTextSize(2);
  gfx.setTextColor(chg, theme::BG);
  gfx.setCursor(220, 66);
  snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%)   ", q.change, q.changePct);
  gfx.print(buf);

  // Stale note under the price
  gfx.fillRect(10, 92, 260, 10, theme::BG);
  if (staleMin >= 0) {
    gfx.setTextSize(1);
    gfx.setTextColor(theme::ACCENT, theme::BG);
    gfx.setCursor(10, 94);
    snprintf(buf, sizeof(buf), "DELAYED: last update %d min ago", staleMin);
    gfx.print(buf);
  }
  drawnStaleMin = staleMin;

  drawLineChart(CHART_X, CHART_Y, CHART_W, CHART_H, s.intraday, chg, theme::BG, true, q.prevClose);

  // Left column: today. Right column: company stats (fetched weekly, kept current).
  drawPriceStat(0, 0, "OPEN", q.open);
  drawPriceStat(0, 1, "DAY HIGH", q.high);
  drawPriceStat(0, 2, "DAY LOW", q.low);
  drawPriceStat(0, 3, "PREV CLOSE", q.prevClose);

  drawPriceStat(1, 0, "52W HIGH", f.valid ? f.high52 : 0);
  drawPriceStat(1, 1, "52W LOW", f.valid ? f.low52 : 0);
  buf[0] = '\0';
  float pe = s.peRatio();
  if (pe > 0) snprintf(buf, sizeof(buf), "%.1f", pe);
  drawStat(1, 2, "P/E", buf);
  buf[0] = '\0';
  double cap = s.marketCap();
  if (cap > 0) formatMarketCap(buf, sizeof(buf), cap);
  drawStat(1, 3, "MKT CAP", buf);
}

void drawFull(int ticker, bool pinned) {
  display().fillScreen(theme::BG);
  drawHeader(ticker, pinned);
  drawData(ticker);
  drawnVersion = stock(ticker).version;
  drawnBarW = -1;
}

void update(int ticker) {
  bool staleChanged = staleMinutes(stock(ticker)) != drawnStaleMin;
  if (stock(ticker).version == drawnVersion && !staleChanged) return;
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
