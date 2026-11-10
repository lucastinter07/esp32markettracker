#include "ui/price_chart.h"
#include <Arduino.h>
#include <string.h>
#include <time.h>
#include "chart_math.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "ui/theme.h"

static const int16_t LABEL_W = 52;   // price label column on the right
static const int16_t TIME_H = 14;    // time label row underneath
static const int MAX_PRICE_LABELS = 5;
static const int MAX_POINTS = 480;   // 5D: up to 70 backfilled bars + 390 of today's points
static const int MAX_X_LABELS = 8;
static const int MAX_SEPARATORS = 6;

static const char* const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// What a range builder hands to the renderer: points placed across the chart as a
// fraction (0 = left edge, 1 = right edge), plus labels and day separators.
struct ChartData {
  int n;
  float frac[MAX_POINTS];
  float price[MAX_POINTS];
  int labels;
  float labelFrac[MAX_X_LABELS];
  char labelText[MAX_X_LABELS][8];
  int separators;
  float separatorFrac[MAX_SEPARATORS];
  bool showPrevClose;
  char cornerTag[16];      // e.g. "Fri Nov 6" for an older session; "" for none
  const char* empty1;      // message when there's nothing to draw
  const char* empty2;
};
static ChartData data;   // static: ~4 KB, too big for the stack

static void resetData() {
  data.n = 0;
  data.labels = 0;
  data.separators = 0;
  data.showPrevClose = false;
  data.cornerTag[0] = '\0';
  data.empty1 = "No chart yet";
  data.empty2 = "";
}

static void addPoint(float frac, float price) {
  if (data.n >= MAX_POINTS) return;
  data.frac[data.n] = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
  data.price[data.n] = price;
  data.n++;
}

static void addLabel(float frac, const char* text) {
  if (data.labels >= MAX_X_LABELS) return;
  data.labelFrac[data.labels] = frac;
  snprintf(data.labelText[data.labels], sizeof(data.labelText[0]), "%s", text);
  data.labels++;
}

// "9:30", "2:00" for a unix time, in Eastern time
static void clockLabel(uint32_t unixTime, char* buf, size_t size) {
  time_t t = unixTime;
  struct tm et;
  localtime_r(&t, &et);
  snprintf(buf, size, "%d:%02d", et.tm_hour % 12 == 0 ? 12 : et.tm_hour % 12, et.tm_min);
}

static bool newestIsToday(const PriceSeries& series) {
  EtTime now;
  if (!timeNowET(now)) return true;
  time_t t = series.timeAt(series.size() - 1);
  struct tm et;
  localtime_r(&t, &et);
  return et.tm_mday == now.day && et.tm_mon + 1 == now.month;
}

static void tagSessionDate(uint32_t unixTime) {
  time_t t = unixTime;
  struct tm et;
  localtime_r(&t, &et);
  snprintf(data.cornerTag, sizeof(data.cornerTag), "%s %s %d", DAYS[et.tm_wday], MONTHS[et.tm_mon], et.tm_mday);
}

// ---------- range builders ----------

// Today's session, 9:30 to the close, points at their time of day.
static void buildDay(const SymbolState& s) {
  const PriceSeries& series = s.intraday;
  data.empty2 = "fills in from 9:30";
  data.showPrevClose = true;
  if (series.size() < 2) return;

  if (!series.hasTimes()) {   // demo prices: evenly spaced, no time labels
    for (size_t i = 0; i < series.size(); i++) addPoint((float)i / (series.size() - 1), series.at(i));
    return;
  }
  // The chart shows the session of its NEWEST point; an older stray point clamps to 9:30.
  uint32_t open, close;
  sessionBounds(series.timeAt(series.size() - 1), open, close);
  float span = (float)(close - open);
  for (size_t i = 0; i < series.size(); i++) {
    uint32_t t = series.timeAt(i);
    addPoint(t < open ? 0 : (t - open) / span, series.at(i));
  }
  // Labels every 90 min (60 min on 1 pm early-close days), starting at 9:30.
  uint32_t step = (close - open) > 4 * 3600 ? 90 * 60 : 60 * 60;
  char buf[8];
  for (uint32_t t = open; t <= close; t += step) {
    clockLabel(t, buf, sizeof(buf));
    addLabel((t - open) / span, buf);
  }
  if (!newestIsToday(series)) tagSessionDate(series.timeAt(series.size() - 1));
}

// The last 60 minutes of today's points.
static void buildHour(const SymbolState& s) {
  const PriceSeries& series = s.intraday;
  data.empty2 = "fills in from 9:30";
  if (series.size() < 2 || !series.hasTimes()) return;

  uint32_t end = series.timeAt(series.size() - 1);
  uint32_t start = end - 3600;
  for (size_t i = 0; i < series.size(); i++) {
    uint32_t t = series.timeAt(i);
    if (t >= start) addPoint((t - start) / 3600.0f, series.at(i));
  }
  if (data.n < 2) {
    data.n = 0;
    data.empty1 = "Not enough points";
    data.empty2 = "in the last hour";
    return;
  }
  // A label on every quarter hour (Eastern offsets are whole hours, so UTC quarters line up).
  char buf[8];
  for (uint32_t t = (start / 900 + 1) * 900; t <= end; t += 900) {
    clockLabel(t, buf, sizeof(buf));
    addLabel((t - start) / 3600.0f, buf);
  }
  if (!newestIsToday(series)) tagSessionDate(end);
}

// The last 5 sessions side by side (nights and weekends left out): backfilled 30-minute
// closes for earlier days, today's own points for the newest day.
static void buildFiveDay(const SymbolState& s) {
  const PriceSeries& series = s.intraday;
  const History& h = s.history;
  data.empty1 = "No 5-day history yet";
  data.empty2 = h.valid ? "" : "needs Twelve Data key";

  // Collect times + prices, oldest first, without duplicating the live session.
  static uint32_t times[MAX_POINTS];
  static float prices[MAX_POINTS];
  int count = 0;
  bool live = series.size() > 0 && series.hasTimes();
  uint32_t liveOpen = 0, liveClose = 0;
  if (live) sessionBounds(series.timeAt(series.size() - 1), liveOpen, liveClose);
  for (int i = 0; i < h.barCount && count < MAX_POINTS; i++) {
    uint32_t t = h.barTime[i];
    if (live && t > liveOpen && t <= liveClose) continue;   // that day comes from live points
    times[count] = t;
    prices[count] = h.barClose[i];
    count++;
  }
  if (live) {
    for (size_t i = 0; i < series.size() && count < MAX_POINTS; i++) {
      times[count] = series.timeAt(i);
      prices[count] = series.at(i);
      count++;
    }
  }
  if (count < 2) return;

  // Find the distinct sessions (by their 9:30 open) and keep the last 5.
  static uint32_t sessionOpen[MAX_POINTS];
  uint32_t opens[16];
  int sessions = 0;
  for (int i = 0; i < count; i++) {
    uint32_t open, close;
    sessionBounds(times[i] > 0 ? times[i] - 1 : 0, open, close);   // -1: a 16:00 bar end belongs to its day
    sessionOpen[i] = open;
    if (sessions == 0 || opens[sessions - 1] != open) {
      if (sessions < 16) opens[sessions++] = open;
    }
  }
  int first = sessions > 5 ? sessions - 5 : 0;
  int slots = sessions - first;

  for (int i = 0; i < count; i++) {
    int slot = -1;
    for (int k = first; k < sessions; k++) {
      if (opens[k] == sessionOpen[i]) slot = k - first;
    }
    if (slot < 0) continue;   // older than the 5 kept sessions
    uint32_t open = sessionOpen[i], close, dummy;
    sessionBounds(open, dummy, close);
    float within = times[i] <= open ? 0 : (float)(times[i] - open) / (close - open);
    if (within > 1) within = 1;
    addPoint((slot + within) / slots, prices[i]);
  }
  // Weekday names in the middle of each slot, separator lines between days.
  for (int k = 0; k < slots; k++) {
    time_t t = opens[first + k];
    struct tm et;
    localtime_r(&t, &et);
    addLabel((k + 0.5f) / slots, DAYS[et.tm_wday]);
    if (k > 0 && data.separators < MAX_SEPARATORS) data.separatorFrac[data.separators++] = (float)k / slots;
  }
}

// ~21 daily closes before today, plus today's price as the last point.
static void buildMonth(const SymbolState& s) {
  const History& h = s.history;
  data.empty1 = "No 1-month history yet";
  data.empty2 = h.valid ? "" : "needs Twelve Data key";
  EtTime now;
  if (!timeNowET(now)) return;
  int32_t current = lastSessionDay(now);

  int before = 0;
  while (before < h.dailyCount && h.dailyDay[before] < current) before++;
  int start = before > 21 ? before - 21 : 0;
  int total = (before - start) + (s.hasData ? 1 : 0);
  if (total < 2) return;

  static int32_t days[HISTORY_DAYS + 1];
  int k = 0;
  for (int i = start; i < before; i++, k++) {
    days[k] = h.dailyDay[i];
    addPoint((float)k / (total - 1), h.dailyClose[i]);
  }
  if (s.hasData) {
    days[k] = current;
    addPoint(1.0f, s.quote.price);
  }
  // Dates on about 4 evenly spaced days, e.g. "Oct 12"
  int step = total > 4 ? (total - 1) / 4 : 1;
  for (int i = 0; i < total; i += step) {
    time_t t = (time_t)days[i] * 86400 + 12 * 3600;   // midday UTC: safely inside that date
    struct tm d;
    gmtime_r(&t, &d);
    char buf[8];
    snprintf(buf, sizeof(buf), "%s %d", MONTHS[d.tm_mon], d.tm_mday);
    addLabel((float)i / (total - 1), buf);
  }
}

// ---------- renderer ----------

static void smallText(int16_t x, int16_t y, uint16_t color, uint16_t bg, const char* s) {
  DFRobot_GDL& gfx = display();
  gfx.setTextSize(1);   // 6 px wide per character, 8 px tall
  gfx.setTextColor(color, bg);
  gfx.setCursor(x, y);
  gfx.print(s);
}

static int16_t clampI(int16_t v, int16_t lo, int16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void drawRangeChart(const SymbolState& s, const ChartBox& box, ChartRange range) {
  resetData();
  switch (range) {
    case ChartRange::Hour:    buildHour(s); break;
    case ChartRange::Day:     buildDay(s); break;
    case ChartRange::FiveDay: buildFiveDay(s); break;
    case ChartRange::Month:   buildMonth(s); break;
  }

  DFRobot_GDL& gfx = display();
  const Quote& q = s.quote;
  // The plotted area, inside the box, leaving room for the labels.
  const int16_t px = box.x + 2, py = box.y + 4;
  const int16_t pw = box.w - LABEL_W - 4, ph = box.h - TIME_H - 6;
  const int16_t pRight = px + pw - 1, pBottom = py + ph - 1;
  auto toX = [&](float frac) { return (int16_t)(px + frac * (pw - 1)); };

  gfx.fillRect(box.x, box.y, box.w, box.h, theme::BG);

  if (data.n < 2) {
    gfx.drawRect(px - 1, py - 1, pw + 2, ph + 2, theme::FRAME);
    gfx.setTextSize(2);
    gfx.setTextColor(theme::LABEL, theme::BG);
    gfx.setCursor(px + pw / 2 - (int16_t)strlen(data.empty1) * 6, py + ph / 2 - 18);
    gfx.print(data.empty1);
    gfx.setCursor(px + pw / 2 - (int16_t)strlen(data.empty2) * 6, py + ph / 2 + 4);
    gfx.print(data.empty2);
    return;
  }

  // ---- price axis: round label values covering the prices (and the previous close) ----
  float lo = data.price[0], hi = data.price[0];
  for (int i = 1; i < data.n; i++) {
    if (data.price[i] < lo) lo = data.price[i];
    if (data.price[i] > hi) hi = data.price[i];
  }
  bool prevLine = data.showPrevClose && q.prevClose > 0;
  if (prevLine) {
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

  // ---- labels underneath, with faint vertical gridlines; brighter lines between days ----
  for (int i = 0; i < data.labels; i++) {
    int16_t x = toX(data.labelFrac[i]);
    if (data.separators == 0) gfx.drawFastVLine(x, py, ph, theme::GRID);
    int16_t w = strlen(data.labelText[i]) * 6;
    smallText(clampI(x - w / 2, px, pRight - w), pBottom + 5, theme::LABEL, theme::BG, data.labelText[i]);
  }
  for (int i = 0; i < data.separators; i++) gfx.drawFastVLine(toX(data.separatorFrac[i]), py, ph, theme::FRAME);

  gfx.drawRect(px - 1, py - 1, pw + 2, ph + 2, theme::FRAME);

  if (data.cornerTag[0]) {
    smallText(pRight - (int16_t)strlen(data.cornerTag) * 6 - 3, py + 3, theme::ACCENT, theme::BG, data.cornerTag);
  }

  // ---- previous close (1D only): dashed line with its value ----
  if (prevLine) {
    int16_t y = toY(q.prevClose);
    for (int16_t dx = 0; dx < pw; dx += 8) gfx.drawFastHLine(px + dx, y, 4, theme::LABEL);
    snprintf(buf, sizeof(buf), "PREV %.2f", q.prevClose);
    int16_t ly = (y - 11 >= py + 1) ? y - 11 : y + 4;   // above the line, or below near the top
    smallText(px + 4, ly, theme::LABEL, theme::BG, buf);
  }

  // ---- the price line, 2 px thick, colored by the range's change ----
  float ref;
  float last = data.price[data.n - 1];
  bool up = rangeReference(s, range, ref) ? last >= ref : last >= data.price[0];
  uint16_t color = up ? theme::UP : theme::DOWN;
  int16_t prevX = toX(data.frac[0]), prevY = toY(data.price[0]);
  for (int i = 1; i < data.n; i++) {
    int16_t x = toX(data.frac[i]), y = toY(data.price[i]);
    gfx.drawLine(prevX, prevY, x, y, color);
    gfx.drawLine(prevX, prevY + 1, x, y + 1, color);
    prevX = x;
    prevY = y;
  }

  // ---- high / low over the range: small triangles pointing at them, with values ----
  int iMax = 0, iMin = 0;
  for (int i = 1; i < data.n; i++) {
    if (data.price[i] > data.price[iMax]) iMax = i;
    if (data.price[i] < data.price[iMin]) iMin = i;
  }
  int16_t hx = toX(data.frac[iMax]), hy = toY(data.price[iMax]);
  int16_t lx = toX(data.frac[iMin]), ly2 = toY(data.price[iMin]);
  gfx.fillTriangle(hx - 4, hy - 8, hx + 4, hy - 8, hx, hy - 3, theme::TEXT);
  gfx.fillTriangle(lx - 4, ly2 + 8, lx + 4, ly2 + 8, lx, ly2 + 3, theme::TEXT);
  snprintf(buf, sizeof(buf), "H %.2f", data.price[iMax]);
  int16_t tw = strlen(buf) * 6;
  smallText(clampI(hx - tw / 2, px + 2, pRight - tw), clampI(hy - 18, py + 2, pBottom - 8),
            theme::TEXT, theme::BG, buf);
  snprintf(buf, sizeof(buf), "L %.2f", data.price[iMin]);
  tw = strlen(buf) * 6;
  smallText(clampI(lx - tw / 2, px + 2, pRight - tw), clampI(ly2 + 10, py + 2, pBottom - 8),
            theme::TEXT, theme::BG, buf);

  // ---- last price: a tag in the label column, in the line's color ----
  int16_t tagY = clampI(toY(last) - 6, box.y, pBottom - 6);
  gfx.fillRect(pRight + 3, tagY, LABEL_W - 1, 12, color);
  snprintf(buf, sizeof(buf), "%.*f", decimals, last);
  smallText(pRight + 6, tagY + 2, theme::BG, color, buf);
}

bool rangeReference(const SymbolState& s, ChartRange range, float& reference) {
  if (!s.hasData) return false;
  EtTime now;
  switch (range) {
    case ChartRange::Hour: {
      const PriceSeries& series = s.intraday;
      if (series.size() < 2 || !series.hasTimes()) return false;
      uint32_t end = series.timeAt(series.size() - 1);
      return series.firstAtOrAfter(end - 3600, reference);
    }
    case ChartRange::Day:
      reference = s.quote.prevClose;
      return reference > 0;
    case ChartRange::FiveDay:
      return timeNowET(now) && closeSessionsBack(s.history, lastSessionDay(now), 5, reference);
    case ChartRange::Month:
      return timeNowET(now) && closeSessionsBack(s.history, lastSessionDay(now), 21, reference);
  }
  return false;
}

const char* rangeName(ChartRange range) {
  switch (range) {
    case ChartRange::Hour:    return "1H";
    case ChartRange::Day:     return "1D";
    case ChartRange::FiveDay: return "5D";
    case ChartRange::Month:   return "1M";
  }
  return "";
}
