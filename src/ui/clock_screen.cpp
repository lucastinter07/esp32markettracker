#include "ui/clock_screen.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "earnings.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace clockui {

using ui::Align;

// Layout (480 x 320), as on the design canvas (K2)
static const int16_t LEFT_X = 16;
static const int16_t DAY_Y = 16, DATE_Y = 38, TIME_Y = 76, PHASE_Y = 148;
static const uint8_t TIME_SIZE = 7;               // 56 px tall; "12:59 PM" still fits the left column
static const int16_t CARD_X = 16, CARD_Y = 182, CARD_W = 208, CARD_H = 106;
static const int16_t PANEL_X = 240, PANEL_Y = 16, PANEL_W = 224, PANEL_H = 272;
static const int16_t ROW_X = 252, ROW_RIGHT = 452, ROW_Y0 = 42, ROW_PITCH = 22;
static const int16_t EARN_RULE_Y = 200, EARN_TITLE_Y = 210, EARN_Y0 = 226, EARN_PITCH = 24;

static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* const DAYS_LONG[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// What's on screen, so update() only redraws what changed.
static char drawnTime[8];
static char drawnDate[24];
static char drawnPhase[24];
static char drawnCard[40];
static uint32_t drawnDataSum = 0;
static int32_t drawnDay = -1;

// "45M", "11H 43M", "2D 18H"
static void formatWait(char* buf, size_t size, int32_t minutes) {
  if (minutes < 60) snprintf(buf, size, "%ldM", (long)minutes);
  else if (minutes < 24 * 60) snprintf(buf, size, "%ldH %02ldM", (long)(minutes / 60), (long)(minutes % 60));
  else snprintf(buf, size, "%ldD %ldH", (long)(minutes / 1440), (long)((minutes % 1440) / 60));
}

// The big time with a narrow colon (the font's colon only uses 2 of its 5 columns), so
// "12:59" plus AM/PM fits left of the panel. Returns the x after the last digit.
static int16_t drawBigTime(int16_t x, int16_t y, const char* t, uint16_t fg, uint16_t bg) {
  const int16_t cell = ui::CHAR_W * TIME_SIZE;   // 42
  const int16_t inkW = 5 * TIME_SIZE;            // a digit's lit width
  int16_t end = x;
  for (const char* c = t; *c; c++) {
    char one[2] = {*c, '\0'};
    if (*c == ':') {
      ui::text(x - TIME_SIZE, y, one, TIME_SIZE, fg, bg);   // its lit columns start 1 column in
      x += 3 * TIME_SIZE;
    } else {
      ui::text(x, y, one, TIME_SIZE, fg, bg);
      end = x + inkW;
      x += cell;
    }
  }
  return end;
}

static void drawLeft(const EtTime& now, bool force) {
  const ui::Palette& p = ui::pal();
  char date[24];
  snprintf(date, sizeof(date), "%s|%s %d", DAYS_LONG[weekdayOf(now.year, now.month, now.day)], MONTHS[now.month - 1],
           now.day);
  if (force || strcmp(date, drawnDate) != 0) {
    strcpy(drawnDate, date);
    display().fillRect(LEFT_X, DAY_Y, 220, 46, p.bg);
    char* bar = strchr(date, '|');
    *bar = '\0';
    ui::text(LEFT_X, DAY_Y, date, 2, p.dim, p.bg);
    ui::text(LEFT_X, DATE_Y, bar + 1, 3, p.text, p.bg);
  }

  char t[8];
  int hour12 = now.hour % 12 == 0 ? 12 : now.hour % 12;
  snprintf(t, sizeof(t), "%d:%02d", hour12, now.minute);
  if (force || strcmp(t, drawnTime) != 0) {
    strcpy(drawnTime, t);
    display().fillRect(LEFT_X, TIME_Y, 220, 8 * TIME_SIZE, p.bg);
    int16_t x = drawBigTime(LEFT_X, TIME_Y, t, p.text, p.bg);
    ui::text(x + 4, TIME_Y + 8 * TIME_SIZE - 16, now.hour < 12 ? "AM" : "PM", 2, p.dim, p.bg);
  }

  MarketStatus s = marketStatus(now);
  const char* phase = "MARKET CLOSED";
  switch (s.phase) {
    case MarketPhase::PreMarket:  phase = "PRE-MARKET"; break;
    case MarketPhase::AfterHours: phase = "AFTER HOURS"; break;
    case MarketPhase::Open:       phase = "MARKET OPEN"; break;
    case MarketPhase::Closed:     phase = s.holiday ? "MARKET HOLIDAY" : "MARKET CLOSED"; break;
  }
  if (force || strcmp(phase, drawnPhase) != 0) {
    snprintf(drawnPhase, sizeof(drawnPhase), "%s", phase);
    display().fillRect(LEFT_X, PHASE_Y, 220, 16, p.bg);
    ui::text(LEFT_X, PHASE_Y, phase, 2, p.dim, p.bg);
  }
}

// The card under it: "NEXT OPEN / FRI 9:30 AM / IN 11H 43M / PRE-MARKET FROM 4:00 AM"
static void drawCard(const EtTime& now, bool force) {
  MarketStatus s = marketStatus(now);
  char wait[16], key[sizeof(drawnCard)];
  formatWait(wait, sizeof(wait), s.minutesToOpen);
  const char* day = s.nextOpenIsToday ? "TODAY" : DAYS[s.nextOpenWeekday];
  snprintf(key, sizeof(key), "%s|%s|%d", day, wait, (int)s.phase);
  if (!force && strcmp(key, drawnCard) == 0) return;
  strcpy(drawnCard, key);

  const ui::Palette& p = ui::pal();
  display().fillRect(CARD_X, CARD_Y, CARD_W, CARD_H, p.panel);
  int16_t x = CARD_X + 12;
  ui::text(x, CARD_Y + 12, "NEXT OPEN", 1, p.dim, p.panel);
  char line[24];
  snprintf(line, sizeof(line), "%s 9:30 AM", day);
  ui::text(x, CARD_Y + 28, line, 2, p.text, p.panel);
  ui::text(x, CARD_Y + 56, "IN", 2, p.dim, p.panel);
  ui::text(x + 36, CARD_Y + 52, wait, 3, p.up, p.panel);
  const char* note = s.phase == MarketPhase::PreMarket ? "PRE-MARKET TRADING NOW" : "PRE-MARKET FROM 4:00 AM";
  ui::text(x, CARD_Y + 86, note, 1, p.faint, p.panel);
}

// Right panel: last close of every symbol, then this week's earnings.
static void drawPanel(const EtTime& now) {
  const ui::Palette& p = ui::pal();
  display().fillRect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, p.panel);
  int y, m, d;
  civilFromDay(lastSessionDay(now), y, m, d);
  char buf[32];
  snprintf(buf, sizeof(buf), "LAST CLOSE  %s", DAYS[weekdayOf(y, m, d)]);
  ui::text(ROW_X, PANEL_Y + 10, buf, 1, p.dim, p.panel);

  for (int row = 0; row < NUM_SYMBOLS; row++) {
    // The 2 index ETFs first, then the 5 stocks
    const SymbolState& s = row < cfg::NUM_INDEXES ? indexEtf(row) : stock(row - cfg::NUM_INDEXES);
    int16_t ry = ROW_Y0 + row * ROW_PITCH;
    ui::text(ROW_X, ry, s.symbol, 2, p.text, p.panel);
    if (s.hasData) {
      snprintf(buf, sizeof(buf), "%+.2f%%", s.quote.changePct);
      ui::text(ROW_RIGHT, ry, buf, 2, s.quote.change >= 0 ? p.up : p.down, p.panel, Align::Right);
    } else {
      ui::text(ROW_RIGHT, ry, "--", 2, p.dim, p.panel, Align::Right);
    }
  }

  // Earnings in the next 7 days, soonest first (room for 2)
  display().drawFastHLine(ROW_X, EARN_RULE_Y, ROW_RIGHT - ROW_X, p.line);
  ui::text(ROW_X, EARN_TITLE_Y, "EARNINGS THIS WEEK", 1, p.dim, p.panel);
  int32_t today = dayNumber(now.year, now.month, now.day);
  int order[cfg::NUM_TICKERS], count = 0;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    const Earnings& e = stock(i).earnings;
    if (!e.valid || e.day == 0 || e.day < today || e.day - today > 7) continue;
    int k = count++;
    while (k > 0 && stock(order[k - 1]).earnings.day > e.day) {
      order[k] = order[k - 1];
      k--;
    }
    order[k] = i;
  }
  if (count == 0) ui::text(ROW_X, EARN_Y0, "NONE", 2, p.faint, p.panel);
  for (int k = 0; k < count && k < 2; k++) {
    const SymbolState& s = stock(order[k]);
    int16_t ry = EARN_Y0 + k * EARN_PITCH;
    char when[8];
    snprintf(when, sizeof(when), "%s", s.earnings.hour);
    for (char* c = when; *c; c++) *c = (char)toupper((unsigned char)*c);
    int32_t away = s.earnings.day - today;
    civilFromDay(s.earnings.day, y, m, d);
    if (away == 0) snprintf(buf, sizeof(buf), "TODAY %s", when);
    else if (away < 7) snprintf(buf, sizeof(buf), "%s %s", DAYS[weekdayOf(y, m, d)], when);
    else snprintf(buf, sizeof(buf), "%d/%d %s", m, d, when);   // a week out: the weekday would be ambiguous
    ui::text(ROW_X, ry, s.symbol, 2, p.text, p.panel);
    ui::text(ROW_RIGHT, ry, buf, 2, p.dim, p.panel, Align::Right);
  }
}

static uint32_t dataSum() {
  uint32_t sum = 0;
  for (int i = 0; i < NUM_SYMBOLS; i++) sum += symbolByIndex(i).version;
  return sum;
}

static void drawAll(bool force) {
  EtTime now;
  if (!timeNowET(now)) return;
  drawLeft(now, force);
  drawCard(now, force);
  int32_t today = dayNumber(now.year, now.month, now.day);
  uint32_t sum = dataSum();
  if (force || sum != drawnDataSum || today != drawnDay) {
    drawPanel(now);
    drawnDataSum = sum;
    drawnDay = today;
  }
}

void drawFull() {
  const ui::Palette& p = ui::pal();
  display().fillScreen(p.bg);
  ui::text(PANEL_X + PANEL_W, 302, "TAP FOR PRICES", 1, p.faint, p.bg, Align::Right);
  drawAll(true);
}

void update() {
  drawAll(false);
}

}  // namespace clockui
