#include "ui/detail_screen.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "data/targets.h"
#include "hal/display.h"
#include "net/time_sync.h"
#include "ui/gfx.h"
#include "ui/palette.h"
#include "ui/price_chart.h"

namespace detail {

using ui::Align;

// Layout (480 x 320), as on the design canvas (page "FINAL - Layout A - Graphite")
static const int16_t SYMBOL_X = 100, NAME_X = 156;
static const int16_t PRICE_Y = 32;                   // size 4 price; change and range name at y 46
static const int16_t BODY_Y = 66, BODY_H = 214;      // page content: y 66 .. 279
static const ChartBox DAY_BOX = {8, 78, 396, 158};   // plot area; labels right, time row under
static const ChartBox CMP_BOX = {8, 92, 396, 144};   // compare: legend row above the plot
static const int16_t BTN_Y = 282, BTN_H = 30;
static const int16_t RANGE_X = 8, RANGE_W = 64;      // 4 range buttons in one frame
static const int16_t STATS_X = 274, STATS_W = 92;
static const int16_t CMP_X = 374, CMP_W = 98;
static const int16_t COUNT_Y = 315, COUNT_H = 3;     // auto-mode countdown under the buttons
static const int16_t TILE_W = 113, TILE_H = 66;      // stats: 4 x 2 tiles + earnings tile
static const char* const RANGE_NAMES[4] = {"1H", "1D", "5D", "1M"};

static Page page = Page::Day;
static bool compare = false;          // chart pages show the stock vs SPY in % (8B)
static uint32_t drawnVersion = 0;
static uint32_t drawnBenchVersion = 0;   // SPY's version when the compare chart was drawn
static int drawnStaleMin = -1;   // minutes since the last quote when stale, -1 when fresh
static int16_t drawnBarW = -1;

static bool isChartPage() {
  return page != Page::Stats;
}

static ChartRange pageRange() {
  return (ChartRange)page;   // Hour, Day, FiveDay, Month line up with ChartRange
}

// Header text that isn't the main ink: halfway between the bar and its ink color.
static uint16_t barDim() {
  return ui::mix(ui::pal().bar, ui::pal().ink, 128);
}

// How many whole minutes old the quote is when it counts as stale, otherwise -1.
static int staleMinutes(const SymbolState& s) {
  uint32_t nowMs = millis();
  if (!isStale(s, nowMs, marketIsOpenNow())) return -1;
  return (nowMs - s.lastUpdateMs) / 60000;
}

// Prices in at most 7 characters, so a value always fits its tile.
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

static void upperCase(char* s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

// ---------- header bar ----------

static void drawBadge(bool pinned) {
  const ui::Palette& p = ui::pal();
  ui::text(cfg::SCREEN_W - 6, 9, pinned ? "PINNED" : "  AUTO", 1, barDim(), p.bar, Align::Right);
}

static void drawHeader(const SymbolState& s, bool pinned) {
  const ui::Palette& p = ui::pal();
  ui::headerBar("< BACK", nullptr);
  int16_t x = ui::text(SYMBOL_X, 4, s.symbol, 2, p.ink, p.bar) + 8;
  if (s.profile.valid) {
    if (x < NAME_X) x = NAME_X;
    char name[sizeof(s.profile.name)];
    snprintf(name, sizeof(name), "%s", s.profile.name);
    upperCase(name);
    char shown[sizeof(name)];
    ui::fitText(name, 1, cfg::SCREEN_W - 6 - 6 * 6 - 12 - x, shown, sizeof(shown));   // up to "PINNED"
    ui::text(x, 9, shown, 1, barDim(), p.bar);
  }
  drawBadge(pinned);
}

// ---------- price row ----------

// Price (grey when stale), the change over the page's range (or vs SPY), the range's
// name on the right, and "DELAYED" above it when the price may be out of date.
static void drawTop(const SymbolState& s) {
  const ui::Palette& p = ui::pal();
  int staleMin = staleMinutes(s);
  display().fillRect(0, PRICE_Y - 6, cfg::SCREEN_W, BODY_Y - PRICE_Y + 6, p.bg);
  char buf[48];
  formatPrice(buf, sizeof(buf), s.quote.price);
  int16_t x = ui::text(8, PRICE_Y, buf, 4, staleMin >= 0 ? p.dim : p.text, p.bg) + 14;

  ChartRange range = isChartPage() ? pageRange() : ChartRange::Day;
  const char* note = isChartPage() ? rangeName(range) : "TODAY";
  ui::text(cfg::SCREEN_W - 8, PRICE_Y + 14, note, 2, p.dim, p.bg, Align::Right);
  int16_t room = cfg::SCREEN_W - 8 - ui::textWidth(note, 2) - 12 - x;

  float ref, stockPct, spyPct;
  uint16_t color = p.dim;
  if (compare && isChartPage() && compareFigures(s, benchmark(), range, stockPct, spyPct)) {
    snprintf(buf, sizeof(buf), "%+.2f%% VS %s %+.2f%%", stockPct, benchmark().symbol, spyPct);
    color = stockPct >= 0 ? p.up : p.down;
  } else if (rangeReference(s, range, ref)) {
    float delta = s.quote.price - ref;
    snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%)", delta, delta / ref * 100.0f);
    color = delta >= 0 ? p.up : p.down;
  } else {
    snprintf(buf, sizeof(buf), "--");   // not enough history for this range yet
  }
  char shown[48];
  ui::fitText(buf, 2, room, shown, sizeof(shown));
  ui::text(x, PRICE_Y + 14, shown, 2, color, p.bg);

  if (staleMin >= 0) {
    snprintf(buf, sizeof(buf), "DELAYED: %d MIN AGO", staleMin);
    ui::text(cfg::SCREEN_W - 8, PRICE_Y + 2, buf, 1, p.target, p.bg, Align::Right);
  }
  drawnStaleMin = staleMin;
}

// ---------- buttons ----------

// A button inside its frame: the fill starts 1 px in and is fillW wide.
static void drawButton(int16_t x, int16_t w, int16_t fillW, const char* label, bool on) {
  const ui::Palette& p = ui::pal();
  uint16_t bg = on ? p.accent : p.bg;
  display().fillRect(x + 1, BTN_Y + 1, fillW, BTN_H - 2, bg);
  ui::text(x + w / 2, BTN_Y + 7, label, 2, on ? p.accentInk : p.dim, bg, Align::Center);
}

static void drawButtons() {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  // [ 1H | 1D | 5D | 1M ]
  gfx.drawRect(RANGE_X, BTN_Y, 4 * RANGE_W + 1, BTN_H, p.edge);
  for (int i = 0; i < 4; i++) {
    int16_t x = RANGE_X + i * RANGE_W;
    if (i) gfx.drawFastVLine(x, BTN_Y, BTN_H, p.edge);
    drawButton(x, RANGE_W, RANGE_W - 1, RANGE_NAMES[i], (int)page == i);
  }
  // [ STATS ]
  gfx.drawRect(STATS_X, BTN_Y, STATS_W, BTN_H, p.edge);
  drawButton(STATS_X, STATS_W, STATS_W - 2, "STATS", page == Page::Stats);
  // [ box VS SPY ]: a checkbox, since it's an on/off setting rather than a page
  gfx.drawRect(CMP_X, BTN_Y, CMP_W, BTN_H, p.edge);
  uint16_t bg = compare ? p.accent : p.bg, ink = compare ? p.accentInk : p.dim;
  gfx.fillRect(CMP_X + 1, BTN_Y + 1, CMP_W - 2, BTN_H - 2, bg);
  gfx.drawRect(CMP_X + 8, BTN_Y + 9, 12, 12, ink);
  if (compare) gfx.fillRect(CMP_X + 11, BTN_Y + 12, 6, 6, ink);
  ui::text(CMP_X + 26, BTN_Y + 7, "VS SPY", 2, ink, bg);
}

// ---------- stats page ----------

static void drawTile(int i, const char* label, const char* value) {
  const ui::Palette& p = ui::pal();
  int16_t x = 8 + (i % 4) * (TILE_W + 4), y = 76 + (i / 4) * (TILE_H + 6);
  display().fillRect(x, y, TILE_W, TILE_H, p.panel);
  ui::text(x + 10, y + 10, label, 1, p.dim, p.panel);
  ui::text(x + 10, y + 36, value[0] ? value : "--", 2, value[0] ? p.text : p.dim, p.panel);
}

static void drawPriceTile(int i, const char* label, float v) {
  char buf[12] = "";
  if (v > 0) formatPrice(buf, sizeof(buf), v);
  drawTile(i, label, buf);
}

// "WED NOV 19 . AFTER CLOSE" on the left, "IN 7 DAYS" on the right.
static void drawEarningsTile(const SymbolState& s) {
  static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                       "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  const ui::Palette& p = ui::pal();
  const int16_t y = 222;
  display().fillRect(8, y, 464, 52, p.panel);
  ui::text(18, y + 10, "NEXT EARNINGS", 1, p.target, p.panel);
  const Earnings& e = s.earnings;
  EtTime now;
  if (!e.valid || !timeNowET(now)) {
    ui::text(18, y + 26, "--", 2, p.dim, p.panel);
    return;
  }
  if (e.day == 0) {
    ui::text(18, y + 26, "NONE SCHEDULED YET", 2, p.dim, p.panel);
    return;
  }
  int yy, m, d;
  civilFromDay(e.day, yy, m, d);
  char when[24], line[48];
  snprintf(when, sizeof(when), "%s", earningsTimeText(e));
  upperCase(when);
  snprintf(line, sizeof(line), "%s %s %d%s%s", DAYS[weekdayOf(yy, m, d)], MONTHS[m - 1], d,
           when[0] ? " " G_DOT " " : "", when);
  ui::text(18, y + 26, line, 2, p.text, p.panel);
  int away = e.day - dayNumber(now.year, now.month, now.day);
  char right[16];
  if (away <= 0) snprintf(right, sizeof(right), "TODAY");
  else if (away == 1) snprintf(right, sizeof(right), "TOMORROW");
  else snprintf(right, sizeof(right), "IN %d DAYS", away);
  ui::text(462, y + 26, right, 2, p.dim, p.panel, Align::Right);
}

static void drawStatsPage(const SymbolState& s) {
  const Quote& q = s.quote;
  const Fundamentals& f = s.fundamentals;
  drawPriceTile(0, "OPEN", q.open);
  drawPriceTile(1, "PREV CLOSE", q.prevClose);
  drawPriceTile(2, "DAY HIGH", q.high);
  drawPriceTile(3, "DAY LOW", q.low);
  drawPriceTile(4, "52W HIGH", f.valid ? f.high52 : 0);
  drawPriceTile(5, "52W LOW", f.valid ? f.low52 : 0);
  char buf[16] = "";
  float pe = s.peRatio();
  if (pe > 0) snprintf(buf, sizeof(buf), "%.1f", pe);
  drawTile(6, "P/E", buf);
  buf[0] = '\0';
  double cap = s.marketCap();
  if (cap > 0) formatMarketCap(buf, sizeof(buf), cap);
  drawTile(7, "MKT CAP", buf);
  drawEarningsTile(s);
}

// ---------- page body ----------

static void drawBody(int ticker) {
  const SymbolState& s = stock(ticker);
  display().fillRect(0, BODY_Y, cfg::SCREEN_W, BODY_H, ui::pal().bg);
  if (!isChartPage()) {
    drawStatsPage(s);
    return;
  }
  if (compare) {
    drawCompareChart(s, benchmark(), CMP_BOX, pageRange());
    drawnBenchVersion = benchmark().version;
    return;
  }
  float marks[2];   // armed price targets (8C)
  int count = 0;
  for (int side = 0; side < 2; side++) {
    PriceTarget t = targetGet(ticker, side == 0);
    if (targetArmed(t)) marks[count++] = t.price;
  }
  drawRangeChart(s, DAY_BOX, pageRange(), marks, count);
}

void drawFull(int ticker, bool pinned, Page newPage) {
  page = newPage;
  display().fillScreen(ui::pal().bg);
  drawHeader(stock(ticker), pinned);
  drawTop(stock(ticker));
  drawBody(ticker);
  drawButtons();
  drawnVersion = stock(ticker).version;
  drawnBarW = -1;
}

void setPage(int ticker, Page newPage) {
  if (newPage == page) return;
  page = newPage;
  drawTop(stock(ticker));   // the change in the price row depends on the page
  drawBody(ticker);
  drawButtons();
}

void update(int ticker) {
  const SymbolState& s = stock(ticker);
  bool staleChanged = staleMinutes(s) != drawnStaleMin;
  bool benchChanged = compare && isChartPage() && benchmark().version != drawnBenchVersion;
  if (s.version == drawnVersion && !staleChanged && !benchChanged) return;
  drawTop(s);
  if (s.version != drawnVersion || benchChanged) drawBody(ticker);   // a stale tick alone doesn't change the body
  drawnVersion = s.version;
}

void setCompare(int ticker, bool on) {
  if (on == compare) return;
  compare = on;
  drawButtons();
  if (isChartPage()) {
    drawTop(stock(ticker));
    drawBody(ticker);
  }
}

void resetCompare() {
  compare = false;
}

bool compareOn() {
  return compare;
}

bool isChartTap(int16_t x, int16_t y) {
  (void)x;
  return isChartPage() && y >= DAY_BOX.y - 8 && y < DAY_BOX.y + DAY_BOX.h + 4;
}

void drawCountdown(float remaining) {
  if (remaining < 0) remaining = 0;
  const int16_t full = cfg::SCREEN_W - 16;
  int16_t w = (int16_t)(remaining * full);
  if (w == drawnBarW) return;   // only touch the screen when a pixel actually changes
  DFRobot_GDL& gfx = display();
  gfx.fillRect(8, COUNT_Y, w, COUNT_H, ui::pal().dim);
  gfx.fillRect(8 + w, COUNT_Y, full - w, COUNT_H, ui::pal().bg);
  drawnBarW = w;
}

void setPinned(bool pinned) {
  drawBadge(pinned);
  if (pinned) {
    display().fillRect(8, COUNT_Y, cfg::SCREEN_W - 16, COUNT_H, ui::pal().bg);   // no countdown when pinned
    drawnBarW = 0;
  }
}

bool isBackTap(int16_t x, int16_t y) {
  // Generous target: a fingertip is ~40 px wide, so accept taps a bit beyond the text.
  return x < 96 && y < ui::BAR_H + 16;
}

int tabAt(int16_t x, int16_t y) {
  if (y < BTN_Y - 6) return -1;   // a little extra room above the buttons for a fingertip
  if (x < STATS_X - 6) {
    int i = (x - RANGE_X) / RANGE_W;
    return i < 0 ? 0 : (i > 3 ? 3 : i);
  }
  if (x < CMP_X - 4) return (int)Page::Stats;
  return -1;
}

bool isCompareTap(int16_t x, int16_t y) {
  return y >= BTN_Y - 6 && x >= CMP_X - 4;
}

}  // namespace detail
