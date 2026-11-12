#include "ui/detail_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "data/targets.h"
#include "hal/display.h"
#include "net/time_sync.h"
#include "ui/price_chart.h"
#include "ui/theme.h"

namespace detail {

// Layout, top to bottom (480 x 320)
static const int16_t HEADER_H = 40;
static const int16_t BACK_W = 120;
static const int16_t NAME_Y = 46;       // company name line, just below the header
static const int16_t PRICE_Y = 66;      // big price + change
static const int16_t BODY_Y = 110;      // page content starts here...
static const int16_t BODY_H = 174;      // ...and ends at 283
static const int16_t BAR_Y = 284, BAR_H = 4;   // auto-mode countdown, just above the tabs
static const int16_t TAB_Y = 288, TAB_H = 32;
static const int TAB_COUNT = PAGE_COUNT;   // 96 px each
static const char* const TAB_NAMES[TAB_COUNT] = {"1H", "1D", "5D", "1M", "STATS"};

// Stats page: two columns of four rows, labels left, values right-aligned
static const int16_t STAT_COL_X[2] = {10, 250};
static const int16_t STAT_COL_W = 220;
static const int16_t STAT_Y = 118, STAT_ROW_H = 28;   // 6 rows: the last ends at y 274, above the tabs

static Page page = Page::Day;
static bool compare = false;          // chart pages show the stock vs SPY in % (8B)
static uint32_t drawnVersion = 0;
static uint32_t drawnBenchVersion = 0;   // SPY's version when the compare chart was drawn
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
  gfx.setTextColor(pinned ? theme::ACCENT : theme::LABEL, theme::PANEL);
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

static void drawTabs() {
  DFRobot_GDL& gfx = display();
  int16_t w = cfg::SCREEN_W / TAB_COUNT;
  gfx.setTextSize(2);
  for (int i = 0; i < TAB_COUNT; i++) {
    bool selected = i == (int)page;
    uint16_t bg = selected ? theme::HIGHLIGHT : theme::PANEL;
    gfx.fillRect(i * w, TAB_Y, w - 1, TAB_H, bg);   // the 1 px gap separates the tabs
    gfx.setTextColor(selected ? theme::TEXT : theme::LABEL, bg);
    gfx.setCursor(i * w + (w - (int16_t)strlen(TAB_NAMES[i]) * 12) / 2, TAB_Y + 9);
    gfx.print(TAB_NAMES[i]);
  }
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

// One stats row: amber label on the left, value right-aligned at the column's edge.
// "%7s" pads the value on the left, so a shorter new value covers a longer old one.
static void drawStat(int col, int row, const char* label, const char* value,
                     uint16_t valueColor = theme::TEXT) {
  DFRobot_GDL& gfx = display();
  int16_t x = STAT_COL_X[col];
  int16_t y = STAT_Y + row * STAT_ROW_H;
  char padded[12];
  snprintf(padded, sizeof(padded), "%7s", value[0] ? value : "--");
  gfx.setTextSize(2);
  gfx.setTextColor(theme::STAT_LABEL, theme::BG);
  gfx.setCursor(x, y);
  gfx.print(label);
  gfx.setTextColor(valueColor, theme::BG);
  gfx.setCursor(x + STAT_COL_W - 7 * 12, y);
  gfx.print(padded);
}

// Full-width bottom row, e.g. "EARNINGS  Mon Feb 1, after close (84d)".
static void drawEarningsRow(const SymbolState& s) {
  static const char* const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  DFRobot_GDL& gfx = display();
  const Earnings& e = s.earnings;
  int16_t y = STAT_Y + 5 * STAT_ROW_H;
  gfx.fillRect(0, y, cfg::SCREEN_W, 16, theme::BG);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::STAT_LABEL, theme::BG);
  gfx.setCursor(STAT_COL_X[0], y);
  gfx.print("EARNINGS");

  char buf[40];
  EtTime now;
  if (!e.valid || !timeNowET(now)) {
    snprintf(buf, sizeof(buf), "--");
  } else if (e.day == 0) {
    snprintf(buf, sizeof(buf), "none scheduled yet");
  } else {
    int y2, m, d;
    civilFromDay(e.day, y2, m, d);
    int daysAway = e.day - dayNumber(now.year, now.month, now.day);
    const char* when = earningsTimeText(e);
    char away[12];
    if (daysAway == 0) snprintf(away, sizeof(away), "TODAY");
    else snprintf(away, sizeof(away), "%dd", daysAway);
    snprintf(buf, sizeof(buf), "%s %s %d%s%s (%s)", DAYS[weekdayOf(y2, m, d)], MONTHS[m - 1], d,
             when[0] ? ", " : "", when, away);
  }
  gfx.setTextColor(theme::TEXT, theme::BG);
  gfx.setCursor(STAT_COL_X[0] + 8 * 12 + 12, y);   // just after the 8-character label
  gfx.print(buf);
}

// Change over the last `sessions` trading days, from the backfilled daily closes.
// "--" until there's enough history (or without a Twelve Data key).
static void drawChangeStat(int col, int row, const char* label, const SymbolState& s, int sessions) {
  EtTime now;
  float pct;
  if (s.hasData && timeNowET(now) &&
      changeOverSessions(s.history, lastSessionDay(now), s.quote.price, sessions, pct)) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%+.2f%%", pct);
    drawStat(col, row, label, buf, theme::changeColor(pct));
  } else {
    drawStat(col, row, label, "");
  }
}

static void drawPriceStat(int col, int row, const char* label, float v) {
  char buf[12] = "";
  if (v > 0) formatPrice(buf, sizeof(buf), v);
  drawStat(col, row, label, buf);
}

static void drawStatsPage(const SymbolState& s) {
  const Quote& q = s.quote;
  const Fundamentals& f = s.fundamentals;
  char buf[16];

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

  // Bottom row: change over 1 week (5 sessions) and 1 month (21 sessions)
  drawChangeStat(0, 4, "1 WEEK", s, 5);
  drawChangeStat(1, 4, "1 MONTH", s, 21);

  drawEarningsRow(s);
}

// The chart pages map straight onto chart ranges (same order).
static bool isChartPage() {
  return page != Page::Stats;
}

static ChartRange pageRange() {
  return (ChartRange)page;   // Hour, Day, FiveDay, Month line up with ChartRange
}

static void drawBody(int ticker) {
  const SymbolState& s = stock(ticker);
  if (isChartPage()) {
    ChartBox box{4, BODY_Y, cfg::SCREEN_W - 8, BODY_H};
    if (compare) {
      drawCompareChart(s, benchmark(), box, pageRange());
      drawnBenchVersion = benchmark().version;
    } else {
      float marks[2];   // armed price targets (8C)
      int count = 0;
      for (int side = 0; side < 2; side++) {
        PriceTarget t = targetGet(ticker, side == 0);
        if (targetArmed(t)) marks[count++] = t.price;
      }
      drawRangeChart(s, box, pageRange(), marks, count);
    }
  } else {
    drawStatsPage(s);
  }
}

// The shared top: company name, price, change and the stale note.
static void drawTop(const SymbolState& s) {
  DFRobot_GDL& gfx = display();
  const Quote& q = s.quote;
  int staleMin = staleMinutes(s);
  char buf[48];

  // Full company name (from Finnhub's profile, saved in flash). Size 2 fits 38
  // characters across; longer names end in "..".
  gfx.fillRect(0, NAME_Y - 2, cfg::SCREEN_W, 20, theme::BG);
  if (s.profile.valid) {
    const int MAX_CHARS = (cfg::SCREEN_W - 20) / 12;
    if ((int)strlen(s.profile.name) > MAX_CHARS) {
      snprintf(buf, sizeof(buf), "%.*s..", MAX_CHARS - 2, s.profile.name);
    } else {
      snprintf(buf, sizeof(buf), "%s", s.profile.name);
    }
    gfx.setTextSize(2);
    gfx.setTextColor(theme::STAT_LABEL, theme::BG);   // amber, like the stat labels
    gfx.setCursor(10, NAME_Y);
    gfx.print(buf);
  }

  // Big price (grey when stale) + change line
  gfx.setTextSize(4);
  gfx.setTextColor(staleMin >= 0 ? theme::DIM : theme::TEXT, theme::BG);
  gfx.setCursor(10, PRICE_Y);
  // Left-aligned like the name, with the change following right after it.
  snprintf(buf, sizeof(buf), "%.2f", q.price);
  gfx.print(buf);
  int16_t priceEnd = 10 + (int16_t)strlen(buf) * 24;   // size 4 = 24 px per character
  // Clear everything right of the price (an older, longer price or change text).
  gfx.fillRect(priceEnd, PRICE_Y, cfg::SCREEN_W - priceEnd, 32, theme::BG);
  // The change follows the selected chart range (1D, the day's change, on the stats page).
  ChartRange range = isChartPage() ? pageRange() : ChartRange::Day;
  float ref;
  gfx.setTextSize(2);
  gfx.setCursor(priceEnd + 16, PRICE_Y + 14);   // bottom-aligned with the big digits
  if (rangeReference(s, range, ref)) {
    float delta = q.price - ref;
    gfx.setTextColor(theme::changeColor(delta), theme::BG);
    snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%) ", delta, delta / ref * 100.0f);
  } else {
    gfx.setTextColor(theme::LABEL, theme::BG);
    snprintf(buf, sizeof(buf), "-- ");   // not enough history for this range yet
  }
  gfx.print(buf);
  gfx.setTextColor(theme::LABEL, theme::BG);   // which range the change covers
  gfx.print(rangeName(range));

  // Stale note under the price
  gfx.fillRect(10, PRICE_Y + 35, 260, 10, theme::BG);
  if (staleMin >= 0) {
    gfx.setTextSize(1);
    gfx.setTextColor(theme::ACCENT, theme::BG);
    gfx.setCursor(10, PRICE_Y + 36);
    snprintf(buf, sizeof(buf), "DELAYED: last update %d min ago", staleMin);
    gfx.print(buf);
  }
  drawnStaleMin = staleMin;
}

void drawFull(int ticker, bool pinned, Page newPage) {
  page = newPage;
  display().fillScreen(theme::BG);
  drawHeader(ticker, pinned);
  drawTop(stock(ticker));
  drawBody(ticker);
  drawTabs();
  drawnVersion = stock(ticker).version;
  drawnBarW = -1;
}

void setPage(int ticker, Page newPage) {
  if (newPage == page) return;
  page = newPage;
  display().fillRect(0, BODY_Y, cfg::SCREEN_W, BODY_H, theme::BG);
  drawTop(stock(ticker));   // the change under the price depends on the page
  drawBody(ticker);
  drawTabs();
}

void update(int ticker) {
  const SymbolState& s = stock(ticker);
  bool staleChanged = staleMinutes(s) != drawnStaleMin;
  bool benchChanged = compare && isChartPage() && benchmark().version != drawnBenchVersion;
  if (s.version == drawnVersion && !staleChanged && !benchChanged) return;
  if (s.version != drawnVersion || staleChanged) drawTop(s);
  if (s.version != drawnVersion || benchChanged) drawBody(ticker);   // a stale tick alone doesn't change the body
  drawnVersion = s.version;
}

void setCompare(int ticker, bool on) {
  if (on == compare) return;
  compare = on;
  if (isChartPage()) drawBody(ticker);
}

void resetCompare() {
  compare = false;
}

bool compareOn() {
  return compare;
}

bool isChartTap(int16_t x, int16_t y) {
  (void)x;
  return isChartPage() && y >= BODY_Y && y < BODY_Y + BODY_H;
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

int tabAt(int16_t x, int16_t y) {
  if (y < TAB_Y - 8) return -1;   // a little extra room above the tabs for a fingertip
  int tab = x / (cfg::SCREEN_W / TAB_COUNT);
  return tab < TAB_COUNT ? tab : -1;
}

}  // namespace detail
