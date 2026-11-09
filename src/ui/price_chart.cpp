#include "ui/price_chart.h"
#include <Arduino.h>
#include <string.h>
#include "chart_math.h"
#include "hal/display.h"
#include "net/time_sync.h"
#include "ui/theme.h"

static const int16_t LABEL_W = 52;   // price label column on the right
static const int16_t TIME_H = 14;    // time label row underneath
static const int MAX_PRICE_LABELS = 5;

// Size-1 text is 6 px wide per character and 8 px tall.
static void smallText(int16_t x, int16_t y, uint16_t color, uint16_t bg, const char* s) {
  DFRobot_GDL& gfx = display();
  gfx.setTextSize(1);
  gfx.setTextColor(color, bg);
  gfx.setCursor(x, y);
  gfx.print(s);
}

static int16_t clampI(int16_t v, int16_t lo, int16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void drawDayChart(const SymbolState& s, const ChartBox& box) {
  DFRobot_GDL& gfx = display();
  const PriceSeries& series = s.intraday;
  const Quote& q = s.quote;

  // The plotted area, inside the box, leaving room for the labels.
  const int16_t px = box.x + 2, py = box.y + 4;
  const int16_t pw = box.w - LABEL_W - 4, ph = box.h - TIME_H - 6;
  const int16_t pRight = px + pw - 1, pBottom = py + ph - 1;

  gfx.fillRect(box.x, box.y, box.w, box.h, theme::BG);

  if (series.size() < 2) {
    gfx.drawRect(px - 1, py - 1, pw + 2, ph + 2, theme::FRAME);
    gfx.setTextSize(2);
    gfx.setTextColor(theme::LABEL, theme::BG);
    gfx.setCursor(px + pw / 2 - 6 * 12, py + ph / 2 - 18);
    gfx.print("No chart yet");
    gfx.setCursor(px + pw / 2 - 9 * 12, py + ph / 2 + 4);
    gfx.print("fills in from 9:30");
    return;
  }

  // ---- price axis: round label values covering the prices and the previous close ----
  float lo = series.minValue(), hi = series.maxValue();
  if (q.prevClose > 0) {
    if (q.prevClose < lo) lo = q.prevClose;
    if (q.prevClose > hi) hi = q.prevClose;
  }
  AxisTicks ticks = niceAxis(lo, hi, MAX_PRICE_LABELS);
  int decimals = labelDecimals(ticks.step, ticks.hi);
  auto toY = [&](float v) { return mapToPixel(v, ticks.lo, ticks.hi, pBottom, py); };
  char buf[24];

  for (int k = 0; k < ticks.count; k++) {
    float v = ticks.lo + k * ticks.step;
    int16_t y = toY(v);
    gfx.drawFastHLine(px, y, pw, theme::GRID);
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    smallText(pRight + 6, y - 3, theme::LABEL, theme::BG, buf);
  }

  // ---- time axis: points placed at their real time of day, 9:30 to the close ----
  bool timed = series.hasTimes();
  uint32_t openUnix = 0, closeUnix = 1;
  if (timed) sessionBounds(series.timeAt(0), openUnix, closeUnix);
  auto toX = [&](size_t i) -> int16_t {
    if (!timed) return mapToPixel((float)i, 0, (float)(series.size() - 1), px, pRight);   // demo prices
    uint32_t t = series.timeAt(i);
    if (t < openUnix) t = openUnix;
    if (t > closeUnix) t = closeUnix;
    return mapToPixel((float)(t - openUnix), 0, (float)(closeUnix - openUnix), px, pRight);
  };

  if (timed) {
    // Labels every 90 min (60 min on 1 pm early-close days), starting at 9:30.
    uint32_t sessionMin = (closeUnix - openUnix) / 60;
    uint32_t stepMin = sessionMin > 240 ? 90 : 60;
    for (uint32_t m = 0; m <= sessionMin; m += stepMin) {
      int16_t x = mapToPixel((float)m, 0, (float)sessionMin, px, pRight);
      gfx.drawFastVLine(x, py, ph, theme::GRID);
      int clock = 9 * 60 + 30 + m;   // minutes since midnight, ET
      int hour12 = (clock / 60) % 12 == 0 ? 12 : (clock / 60) % 12;
      snprintf(buf, sizeof(buf), "%d:%02d", hour12, clock % 60);
      int16_t w = strlen(buf) * 6;
      smallText(clampI(x - w / 2, px, pRight - w), pBottom + 5, theme::LABEL, theme::BG, buf);
    }
  }

  gfx.drawRect(px - 1, py - 1, pw + 2, ph + 2, theme::FRAME);

  // ---- an older session (e.g. Friday's chart on a weekend) gets its date in the corner ----
  EtTime now;
  if (timed && timeNowET(now)) {
    time_t t = series.timeAt(0);
    struct tm et;
    localtime_r(&t, &et);
    if (et.tm_mday != now.day || et.tm_mon + 1 != now.month) {
      static const char* const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      snprintf(buf, sizeof(buf), "%s %s %d", DAYS[et.tm_wday], MONTHS[et.tm_mon], et.tm_mday);
      smallText(pRight - (int16_t)strlen(buf) * 6 - 3, py + 3, theme::ACCENT, theme::BG, buf);
    }
  }

  // ---- previous close: dashed line with its value ----
  if (q.prevClose > 0) {
    int16_t y = toY(q.prevClose);
    for (int16_t dx = 0; dx < pw; dx += 8) gfx.drawFastHLine(px + dx, y, 4, theme::LABEL);
    snprintf(buf, sizeof(buf), "PREV %.2f", q.prevClose);
    int16_t ly = (y - 11 >= py + 1) ? y - 11 : y + 4;   // above the line, or below near the top
    smallText(px + 4, ly, theme::LABEL, theme::BG, buf);
  }

  // ---- the price line, 2 px thick so it reads from a distance ----
  uint16_t color = theme::changeColor(q.change);
  int16_t prevX = toX(0), prevY = toY(series.at(0));
  for (size_t i = 1; i < series.size(); i++) {
    int16_t x = toX(i), y = toY(series.at(i));
    gfx.drawLine(prevX, prevY, x, y, color);
    gfx.drawLine(prevX, prevY + 1, x, y + 1, color);
    prevX = x;
    prevY = y;
  }

  // ---- day high / low: small triangles pointing at the extremes, with values ----
  size_t iMax = series.indexOfMax(), iMin = series.indexOfMin();
  int16_t hx = toX(iMax), hy = toY(series.at(iMax));
  int16_t lx = toX(iMin), ly2 = toY(series.at(iMin));
  gfx.fillTriangle(hx - 4, hy - 8, hx + 4, hy - 8, hx, hy - 3, theme::TEXT);
  gfx.fillTriangle(lx - 4, ly2 + 8, lx + 4, ly2 + 8, lx, ly2 + 3, theme::TEXT);
  snprintf(buf, sizeof(buf), "H %.2f", series.at(iMax));
  int16_t tw = strlen(buf) * 6;
  smallText(clampI(hx - tw / 2, px + 2, pRight - tw), clampI(hy - 18, py + 2, pBottom - 8),
            theme::TEXT, theme::BG, buf);
  snprintf(buf, sizeof(buf), "L %.2f", series.at(iMin));
  tw = strlen(buf) * 6;
  smallText(clampI(lx - tw / 2, px + 2, pRight - tw), clampI(ly2 + 10, py + 2, pBottom - 8),
            theme::TEXT, theme::BG, buf);

  // ---- last price: a tag in the label column, in the line's color ----
  int16_t yLast = toY(series.last());
  int16_t tagY = clampI(yLast - 6, box.y, pBottom - 6);
  gfx.fillRect(pRight + 3, tagY, LABEL_W - 1, 12, color);
  snprintf(buf, sizeof(buf), "%.*f", decimals, series.last());
  smallText(pRight + 6, tagY + 2, theme::BG, color, buf);
}
