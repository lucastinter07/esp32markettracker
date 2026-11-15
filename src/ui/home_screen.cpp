#include "ui/home_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "data/watchlist.h"
#include "hal/display.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "ui/fonts.h"
#include "ui/gfx.h"
#include "ui/palette.h"
#include "ui/status_bar.h"
#include "ui/tab_bar.h"

namespace home {

using ui::Align;

// Layout (480 x 320), as on the design canvas
static const int16_t CHIP_Y = 30, CHIP_H = 24;
static const int16_t CARD_X = 8, CARD_W = 464, CARD_Y0 = 60, CARD_H = 38, CARD_PITCH = 42;
static const int16_t NAME_W = 96;                              // symbol / name column
static const int16_t SPARK_X = CARD_X + 110, SPARK_W = 140, SPARK_H = 28;
static const int16_t PILL_W = 66, PILL_H = 20;
static const int16_t PILL_X = CARD_X + CARD_W - 10 - PILL_W;
static const int16_t PRICE_RIGHT = PILL_X - 10;

// What was last drawn, so update() can skip anything unchanged.
static uint32_t drawnStockVersion[cfg::NUM_TICKERS];
static uint32_t drawnIndexVersion[cfg::NUM_INDEXES];
static bool drawnStale[cfg::NUM_TICKERS];
static int drawnHighlight = -1;
static int32_t drawnDay = -1;   // the earnings note ("earnings in 3d") changes at midnight

static bool marketOpen = false;   // checked once per drawFull()/update()

static bool rowStale(int i) {
  return isStale(stock(i), millis(), marketOpen);
}

static int32_t today() {
  EtTime now;
  return timeNowET(now) ? dayNumber(now.year, now.month, now.day) : -1;
}

// Shown in the status line: demo mode, a problem getting prices, or prices that stopped.
static const char* feedNotice(uint16_t& color) {
  const ui::Palette& p = ui::pal();
  if (cfg::DEMO_PRICES) {
    color = p.target;
    return "DEMO PRICES";
  }
  const char* problem = quoteFeedProblem();
  if (problem) {
    color = p.down;
    return problem;
  }
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    if (rowStale(i)) {
      color = p.target;
      return "PRICES DELAYED";
    }
  }
  return nullptr;
}

static void drawStatus(bool force) {
  uint16_t color = 0;
  const char* notice = feedNotice(color);
  statusBarDraw(force, notice, color);
}

// Intraday sparkline with a soft fill under it, on the card's background.
static void drawSparkline(int16_t x, int16_t y, const PriceSeries& series, uint16_t color, uint16_t cardBg) {
  size_t n = series.size();
  if (n < 2) return;
  float lo = series.minValue(), hi = series.maxValue();
  if (hi - lo < 0.0001f) hi = lo + 0.0001f;
  DFRobot_GDL& gfx = display();
  uint16_t fill = ui::mix(cardBg, color, 45);
  int16_t prevX = 0, prevY = 0;
  for (int16_t col = 0; col < SPARK_W; col++) {
    // The average of the points that fall in this pixel column (smoother than picking one).
    size_t from = (size_t)((uint32_t)col * n / SPARK_W), to = (size_t)((uint32_t)(col + 1) * n / SPARK_W);
    if (to <= from) to = from + 1;
    if (to > n) to = n;
    float v = 0;
    for (size_t k = from; k < to; k++) v += series.at(k);
    v /= (float)(to - from);
    int16_t py = y + (SPARK_H - 1) - (int16_t)((v - lo) / (hi - lo) * (SPARK_H - 1));
    gfx.drawFastVLine(x + col, py, y + SPARK_H - py, fill);
    if (col > 0) gfx.drawLine(x + prevX, prevY, x + col, py, color);
    prevX = col;
    prevY = py;
  }
}

static void drawChip(int k) {
  const ui::Palette& p = ui::pal();
  const SymbolState& s = indexEtf(k);
  int16_t w = (CARD_W - 8) / cfg::NUM_INDEXES;
  int16_t x = CARD_X + k * (w + 8);
  ui::pill(x, CHIP_Y, w, CHIP_H, p.card);
  int16_t afterLabel = ui::textMid(x + 12, CHIP_Y, CHIP_H, Inter10, p.sub, watchlistIndexLabel(k));
  if (!s.hasData) {
    ui::textMid(x + w - 12, CHIP_Y, CHIP_H, Mono12, p.faint, "--", Align::Right);
    return;
  }
  char pct[12], price[12];
  snprintf(pct, sizeof(pct), "%+.2f%%", s.quote.changePct);
  snprintf(price, sizeof(price), "%.2f", s.quote.price);
  int16_t pctLeft = x + w - 12 - ui::textWidth(Mono12, pct);
  ui::textMid(x + w - 12, CHIP_Y, CHIP_H, Mono12, s.quote.change >= 0 ? p.up : p.down, pct, Align::Right);
  if (pctLeft - 6 - ui::textWidth(Mono12, price) > afterLabel + 6) {
    ui::textMid(pctLeft - 6, CHIP_Y, CHIP_H, Mono12, p.text, price, Align::Right);
  }
}

static void drawCard(int i, bool highlighted) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  const SymbolState& s = stock(i);
  bool stale = rowStale(i);
  drawnStale[i] = stale;
  int16_t y = CARD_Y0 + i * CARD_PITCH;
  uint16_t bg = highlighted ? p.cardHi : p.card;
  ui::fillRound(CARD_X, y, CARD_W, CARD_H, 10, bg);
  if (highlighted) gfx.drawRoundRect(CARD_X, y, CARD_W, CARD_H, 10, p.accent);

  // Symbol, and under it the company name, or the earnings date when it's within a week
  ui::text(CARD_X + 10, y + 17, InterBold15, p.text, s.symbol);
  char note[48];
  uint16_t noteColor = p.sub;
  note[0] = '\0';
  int32_t day = today();
  int daysAway = (s.earnings.valid && s.earnings.day != 0 && day >= 0) ? s.earnings.day - day : -1;
  if (daysAway >= 0 && daysAway <= 7) {
    if (daysAway == 0) snprintf(note, sizeof(note), "earnings today");
    else snprintf(note, sizeof(note), "earnings in %dd", daysAway);
    noteColor = p.target;
  } else if (s.profile.valid) {
    snprintf(note, sizeof(note), "%s", s.profile.name);
  }
  char shown[48];
  ui::fitText(Inter10, note, NAME_W, shown, sizeof(shown));
  ui::text(CARD_X + 10, y + 31, Inter10, noteColor, shown);

  if (!s.hasData) {
    ui::textMid(PRICE_RIGHT, y, CARD_H, Inter12, p.faint, "waiting" UI_ELLIPSIS, Align::Right);
    return;
  }
  bool up = s.quote.change >= 0;
  uint16_t color = up ? p.up : p.down;
  drawSparkline(SPARK_X, y + (CARD_H - SPARK_H) / 2, s.intraday, color, bg);

  char buf[16];
  snprintf(buf, sizeof(buf), "%.2f", s.quote.price);
  ui::textMid(PRICE_RIGHT, y, CARD_H, Mono14, stale ? p.faint : p.text, buf, Align::Right);

  int16_t pillY = y + (CARD_H - PILL_H) / 2;
  ui::pill(PILL_X, pillY, PILL_W, PILL_H, ui::mix(bg, color, 45));
  snprintf(buf, sizeof(buf), "%+.2f%%", s.quote.changePct);
  ui::textMid(PILL_X + PILL_W / 2, pillY, PILL_H, Mono12, color, buf, Align::Center);
}

void drawFull(int highlightRow) {
  marketOpen = marketIsOpenNow();
  display().fillScreen(ui::pal().bg);
  drawStatus(true);
  for (int k = 0; k < cfg::NUM_INDEXES; k++) {
    drawChip(k);
    drawnIndexVersion[k] = indexEtf(k).version;
  }
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    drawCard(i, i == highlightRow);
    drawnStockVersion[i] = stock(i).version;
  }
  tabbar::draw(tabbar::Home);
  drawnHighlight = highlightRow;
  drawnDay = today();
}

void update(int highlightRow) {
  marketOpen = marketIsOpenNow();
  drawStatus(false);   // redraws itself only when its text changes
  int32_t day = today();
  bool newDay = day != drawnDay;
  drawnDay = day;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    bool highlightChanged = (i == highlightRow) != (i == drawnHighlight);
    bool staleChanged = rowStale(i) != drawnStale[i];
    if (newDay || highlightChanged || staleChanged || stock(i).version != drawnStockVersion[i]) {
      drawCard(i, i == highlightRow);
      drawnStockVersion[i] = stock(i).version;
    }
  }
  drawnHighlight = highlightRow;
  for (int k = 0; k < cfg::NUM_INDEXES; k++) {
    if (indexEtf(k).version != drawnIndexVersion[k]) {
      drawChip(k);
      drawnIndexVersion[k] = indexEtf(k).version;
    }
  }
}

int rowAt(int16_t x, int16_t y) {
  (void)x;
  if (y < CARD_Y0 - 2 || y >= tabbar::Y - 6) return -1;
  int row = (y - CARD_Y0 + 2) / CARD_PITCH;
  return row < cfg::NUM_TICKERS ? row : -1;
}

bool isIndexTap(int16_t x, int16_t y) {
  (void)x;
  return y >= STATUS_H && y < CARD_Y0 - 2;
}

}  // namespace home
