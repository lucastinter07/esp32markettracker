#include "ui/clock_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "earnings.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "ui/theme.h"

namespace clockui {

// Layout (480 x 320), top to bottom
static const int16_t TIME_Y = 24;        // size 8 digits: 64 px tall
static const int16_t DATE_Y = 102;
static const int16_t OPEN_Y = 128;
static const int16_t RULE1_Y = 156;
static const int16_t STRIP_TITLE_Y = 164;
static const int16_t STRIP_Y = 180;      // symbol, then the change underneath
static const int16_t CELL_W = 68;        // 7 cells: 2 indexes + 5 stocks
static const int16_t RULE2_Y = 230;
static const int16_t EARN_Y = 238;

static const char* const DAY_SHORT[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const DAY_LONG[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* const MONTH_SHORT[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char* const MONTH_LONG[] = {"January", "February", "March",     "April",   "May",      "June",
                                         "July",    "August",   "September", "October", "November", "December"};

// What's on screen, so update() only redraws what changed.
static char drawnTime[8];
static char drawnDate[32];
static char drawnOpen[48];
static uint32_t drawnDataSum = 0;
static int32_t drawnDay = -1;

static void printAt(int16_t x, int16_t y, uint8_t size, uint16_t fg, const char* s) {
  DFRobot_GDL& gfx = display();
  gfx.setTextSize(size);
  gfx.setTextColor(fg, theme::BG);
  gfx.setCursor(x, y);
  gfx.print(s);
}

static void printCentered(int16_t y, uint8_t size, uint16_t fg, const char* s) {
  printAt((cfg::SCREEN_W - (int16_t)strlen(s) * 6 * size) / 2, y, size, fg, s);
}

// "45m", "13h 22m", "2d 18h"
static void formatWait(char* buf, size_t size, int32_t minutes) {
  if (minutes < 60) snprintf(buf, size, "%ldm", (long)minutes);
  else if (minutes < 24 * 60) snprintf(buf, size, "%ldh %ldm", (long)(minutes / 60), (long)(minutes % 60));
  else snprintf(buf, size, "%ldd %ldh", (long)(minutes / 1440), (long)((minutes % 1440) / 60));
}

static void drawTime(const EtTime& now, bool force) {
  char t[8];
  int hour12 = now.hour % 12 == 0 ? 12 : now.hour % 12;
  snprintf(t, sizeof(t), "%d:%02d", hour12, now.minute);
  if (!force && strcmp(t, drawnTime) == 0) return;
  strcpy(drawnTime, t);
  DFRobot_GDL& gfx = display();
  gfx.fillRect(0, TIME_Y, cfg::SCREEN_W, 64, theme::BG);
  const char* ampm = now.hour < 12 ? "AM" : "PM";
  int16_t width = (int16_t)strlen(t) * 48 + 10 + 2 * 18;   // digits (size 8), gap, AM/PM (size 3)
  int16_t x = (cfg::SCREEN_W - width) / 2;
  printAt(x, TIME_Y, 8, theme::TEXT, t);
  printAt(x + (int16_t)strlen(t) * 48 + 10, TIME_Y + 64 - 24 - 2, 3, theme::LABEL, ampm);
}

static void drawDate(const EtTime& now, bool force) {
  char d[32];
  snprintf(d, sizeof(d), "%s, %s %d", DAY_LONG[weekdayOf(now.year, now.month, now.day)], MONTH_LONG[now.month - 1],
           now.day);
  if (!force && strcmp(d, drawnDate) == 0) return;
  strcpy(drawnDate, d);
  display().fillRect(0, DATE_Y, cfg::SCREEN_W, 16, theme::BG);
  printCentered(DATE_Y, 2, theme::LABEL, d);
}

// "Opens Thu 9:30 AM, in 13h 22m" (holiday-aware through marketStatus)
static void drawNextOpen(const EtTime& now, bool force) {
  MarketStatus s = marketStatus(now);
  char wait[16], line[48];
  formatWait(wait, sizeof(wait), s.minutesToOpen);
  snprintf(line, sizeof(line), "%sOpens %s 9:30 AM, in %s", s.holiday ? "Holiday. " : "",
           s.nextOpenIsToday ? "today" : DAY_SHORT[s.nextOpenWeekday], wait);
  if (!force && strcmp(line, drawnOpen) == 0) return;
  strcpy(drawnOpen, line);
  display().fillRect(0, OPEN_Y, cfg::SCREEN_W, 16, theme::BG);
  printCentered(OPEN_Y, 2, theme::ACCENT, line);
}

// One cell of the last-session strip: symbol, then the day's change, each in the largest
// size that fits the 68 px cell.
static void drawCell(int col, const SymbolState& s) {
  int16_t x = 4 + col * CELL_W;
  display().fillRect(x, STRIP_Y, CELL_W - 2, 44, theme::BG);
  uint8_t symSize = strlen(s.symbol) * 12 <= (size_t)(CELL_W - 4) ? 2 : 1;
  printAt(x, STRIP_Y + (symSize == 2 ? 0 : 4), symSize, theme::TEXT, s.symbol);
  char pct[12];
  if (!s.hasData) snprintf(pct, sizeof(pct), "--");
  else snprintf(pct, sizeof(pct), "%+.1f%%", s.quote.changePct);
  uint8_t pctSize = strlen(pct) * 12 <= (size_t)(CELL_W - 4) ? 2 : 1;
  printAt(x, STRIP_Y + 22 + (pctSize == 2 ? 0 : 4), pctSize, s.hasData ? theme::changeColor(s.quote.change) : theme::DIM,
          pct);
}

static void drawStrip(const EtTime& now) {
  display().fillRect(0, STRIP_TITLE_Y, cfg::SCREEN_W, 10, theme::BG);
  int y, m, d;
  civilFromDay(lastSessionDay(now), y, m, d);
  char title[40];
  snprintf(title, sizeof(title), "LAST SESSION: %s %s %d", DAY_SHORT[weekdayOf(y, m, d)], MONTH_SHORT[m - 1], d);
  printAt(8, STRIP_TITLE_Y, 1, theme::LABEL, title);
  int col = 0;
  for (int k = 0; k < cfg::NUM_INDEXES; k++) drawCell(col++, indexEtf(k));
  for (int i = 0; i < cfg::NUM_TICKERS; i++) drawCell(col++, stock(i));
}

// Stocks reporting within 7 days, one per line: "NVDA  Thu Nov 19, after close"
static void drawEarnings(const EtTime& now) {
  display().fillRect(0, EARN_Y, cfg::SCREEN_W, cfg::SCREEN_H - EARN_Y, theme::BG);
  printAt(8, EARN_Y, 1, theme::LABEL, "EARNINGS, NEXT 7 DAYS");
  int32_t today = dayNumber(now.year, now.month, now.day);
  int16_t y = EARN_Y + 14;
  int lines = 0;
  for (int i = 0; i < cfg::NUM_TICKERS; i++) {
    const Earnings& e = stock(i).earnings;
    if (!e.valid || e.day == 0 || e.day < today || e.day - today > 7) continue;
    int yy, m, d;
    civilFromDay(e.day, yy, m, d);
    const char* when = earningsTimeText(e);
    char line[48];
    if (e.day == today) {
      snprintf(line, sizeof(line), "today%s%s", when[0] ? ", " : "", when);
    } else {
      snprintf(line, sizeof(line), "%s %s %d%s%s", DAY_SHORT[weekdayOf(yy, m, d)], MONTH_SHORT[m - 1], d,
               when[0] ? ", " : "", when);
    }
    printAt(8, y, 2, theme::ACCENT, stock(i).symbol);
    printAt(8 + 8 * 12, y, 2, theme::TEXT, line);
    y += 20;
    if (++lines == 3) break;   // room for 3 lines above the bottom edge
  }
  if (lines == 0) printAt(8, y, 2, theme::DIM, "none");
}

static uint32_t dataSum() {
  uint32_t sum = 0;
  for (int i = 0; i < NUM_SYMBOLS; i++) sum += symbolByIndex(i).version;
  return sum;
}

static void drawAll(bool force) {
  EtTime now;
  if (!timeNowET(now)) return;
  drawTime(now, force);
  drawDate(now, force);
  drawNextOpen(now, force);
  int32_t today = dayNumber(now.year, now.month, now.day);
  uint32_t sum = dataSum();
  if (force || sum != drawnDataSum || today != drawnDay) {
    drawStrip(now);
    drawEarnings(now);
    drawnDataSum = sum;
    drawnDay = today;
  }
}

void drawFull() {
  DFRobot_GDL& gfx = display();
  gfx.fillScreen(theme::BG);
  gfx.drawFastHLine(8, RULE1_Y, cfg::SCREEN_W - 16, theme::FRAME);
  gfx.drawFastHLine(8, RULE2_Y, cfg::SCREEN_W - 16, theme::FRAME);
  printAt(cfg::SCREEN_W - 8 - 17 * 6, 6, 1, theme::DIM, "tap to see prices");
  drawAll(true);
}

void update() {
  drawAll(false);
}

}  // namespace clockui
