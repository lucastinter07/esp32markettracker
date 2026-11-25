#include "ui/price_chart.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "chart_math.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "ui/gfx.h"
#include "ui/palette.h"

using ui::Align;

static const int16_t AXIS_GAP = 8;      // plot's right edge -> price label column
static const int16_t TAG_W = 62;        // "T 132.50" / "P 129.90" tags in that column
static const int16_t TIME_GAP = 6;      // plot's bottom -> time row
static const int16_t LEGEND_GAP = 23;   // plot's bottom -> legend row
static const int MAX_AXIS_LABELS = 8;
static const int MAX_POINTS = 480;   // 5D: up to 70 backfilled bars + 390 of today's points
static const int MAX_X_LABELS = 8;
static const int MAX_SEPARATORS = 6;

static const char* const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

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
  char cornerTag[16];      // e.g. "FRI NOV 6" when the chart shows an older session; "" for none
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
  data.empty1 = "NO CHART YET";
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
  data.empty2 = "FILLS IN FROM 9:30";
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
  // Labels every 90 min (60 min on 1 pm early-close days) from 9:30, and the close at the
  // end (a regular label too close to it is left out, e.g. 3:30 before 4:00).
  uint32_t step = (close - open) > 4 * 3600 ? 90 * 60 : 60 * 60;
  char buf[8];
  for (uint32_t t = open; t + step * 3 / 4 <= close; t += step) {
    clockLabel(t, buf, sizeof(buf));
    addLabel((t - open) / span, buf);
  }
  clockLabel(close, buf, sizeof(buf));
  addLabel(1.0f, buf);
  if (!newestIsToday(series)) tagSessionDate(series.timeAt(series.size() - 1));
}

// The last 60 minutes of today's points.
static void buildHour(const SymbolState& s) {
  const PriceSeries& series = s.intraday;
  data.empty2 = "FILLS IN FROM 9:30";
  if (series.size() < 2 || !series.hasTimes()) return;

  uint32_t end = series.timeAt(series.size() - 1);
  uint32_t start = end - 3600;
  for (size_t i = 0; i < series.size(); i++) {
    uint32_t t = series.timeAt(i);
    if (t >= start) addPoint((t - start) / 3600.0f, series.at(i));
  }
  if (data.n < 2) {
    data.n = 0;
    data.empty1 = "NOT ENOUGH POINTS";
    data.empty2 = "IN THE LAST HOUR";
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

// The last 5 sessions side by side (nights and weekends left out), every day at the same
// 30-minute steps: backfilled closes for earlier days; for the newest day (the one the live
// quotes belong to) the backfilled bars where there are any, live quotes for the rest.
static void buildFiveDay(const SymbolState& s) {
  const PriceSeries& series = s.intraday;
  const History& h = s.history;
  data.empty1 = "NO 5-DAY HISTORY YET";
  data.empty2 = h.valid ? "" : "NEEDS TWELVE DATA KEY";

  // Collect times + prices, oldest first; the live session's bars are merged in below.
  static uint32_t times[MAX_POINTS];
  static float prices[MAX_POINTS];
  int count = 0;
  bool live = series.size() > 0 && series.hasTimes();
  uint32_t liveOpen = 0, liveClose = 0;
  if (live) sessionBounds(series.timeAt(series.size() - 1), liveOpen, liveClose);
  for (int i = 0; i < h.barCount && count < MAX_POINTS; i++) {
    uint32_t t = h.barTime[i];
    if (live && t > liveOpen && t <= liveClose) continue;   // the live session comes next
    times[count] = t;
    prices[count] = h.barClose[i];
    count++;
  }
  if (live) {
    count += sessionHalfHours(series, h, liveOpen, liveClose, times + count, prices + count, MAX_POINTS - count);
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
  data.empty1 = "NO 1-MONTH HISTORY YET";
  data.empty2 = h.valid ? "" : "NEEDS TWELVE DATA KEY";
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
  // Dates on about 4 evenly spaced days, e.g. "OCT 12"
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

static void buildRange(const SymbolState& s, ChartRange range) {
  resetData();
  switch (range) {
    case ChartRange::Hour:    buildHour(s); break;
    case ChartRange::Day:     buildDay(s); break;
    case ChartRange::FiveDay: buildFiveDay(s); break;
    case ChartRange::Month:   buildMonth(s); break;
  }
}

// ---------- renderer ----------

static int16_t clampI(int16_t v, int16_t lo, int16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

struct Plot {
  int16_t x, y, w, h;
  float lo, hi;   // the value range from the bottom edge to the top edge
  explicit Plot(const ChartBox& b) : x(b.x), y(b.y), w(b.w), h(b.h), lo(0), hi(1) {}
  int16_t toX(float frac) const { return x + (int16_t)lroundf(frac * (w - 1)); }
  int16_t toY(float v) const { return mapToPixel(v, lo, hi, y + h - 1, y); }
  int16_t axisX() const { return x + w + AXIS_GAP; }
};

// The value range: the data (plus anything else that must show) with 8 % room above and
// below, and round label values inside it (at most MAX_AXIS_LABELS).
static int axisValues(Plot& p, float lo, float hi, float* values, float& step) {
  float span = hi - lo;
  float minSpan = fabsf(hi) * 0.004f > 0.02f ? fabsf(hi) * 0.004f : 0.02f;   // a flat line still gets a range
  if (span < minSpan) {
    float mid = (lo + hi) / 2;
    lo = mid - minSpan / 2;
    hi = mid + minSpan / 2;
    span = minSpan;
  }
  p.lo = lo - span * 0.08f;
  p.hi = hi + span * 0.08f;
  step = niceAxis(p.lo, p.hi, 7).step;   // 7 ticks around the range = about 5 labels inside it
  int n = 0;
  for (float v = ceilf(p.lo / step) * step; v <= p.hi && n < MAX_AXIS_LABELS; v += step) values[n++] = v;
  return n;
}

// Gridlines: 1 px dots every 4 px at each label value, a little brighter than the row
// separators (dots that sparse in the separator color were hard to see).
static uint16_t gridColor() {
  return ui::mix(ui::pal().line, ui::pal().dim, 80);
}

// The grid, the same on every chart: dotted lines across at each price / % label, and dotted
// lines down at the day boundaries (5D) or at each time / date label inside the plot (the
// others; none on the plot's left and right edges).
static void drawGrid(const Plot& p, const float* values, int n) {
  for (int i = 0; i < n; i++) ui::dashedHLine(p.x, p.toY(values[i]), p.w, gridColor(), 1, 3);
  if (data.separators > 0) {
    for (int i = 0; i < data.separators; i++) ui::dashedVLine(p.toX(data.separatorFrac[i]), p.y, p.h, gridColor(), 1, 3);
    return;
  }
  for (int i = 0; i < data.labels; i++) {
    float f = data.labelFrac[i];
    if (f > 0.01f && f < 0.99f) ui::dashedVLine(p.toX(f), p.y, p.h, gridColor(), 1, 3);
  }
}

// SPY's line and legend on the compare chart: a light grey, between the secondary text and
// the main text, so the thin line stands out from the background.
static uint16_t benchColor() {
  return ui::mix(ui::pal().dim, ui::pal().text, 128);
}

// A tag in the label column: colored box with dark text, centered on its line.
struct Tag {
  int16_t y;
  char text[12];
  uint16_t bg;
};

// Labels in the column right of the plot; a tag replaces any label within 12 px of it.
static void drawAxis(const Plot& p, const float* values, int n, const char* const* texts, Tag* tags, int tagCount) {
  const ui::Palette& c = ui::pal();
  // Keep tags 12 px apart (the later one moves down, or up near the bottom).
  for (int i = 1; i < tagCount; i++) {
    for (int j = 0; j < i; j++) {
      if (abs(tags[i].y - tags[j].y) < 12) tags[i].y = tags[j].y + (tags[j].y + 18 > p.y + p.h ? -12 : 12);
    }
  }
  int16_t ax = p.axisX();
  for (int i = 0; i < n; i++) {
    int16_t y = p.toY(values[i]);
    bool hidden = false;
    for (int t = 0; t < tagCount; t++) hidden |= abs(y - tags[t].y) < 12;
    if (!hidden) ui::text(ax, y - 4, texts[i], 1, c.dim, c.bg);
  }
  for (int t = 0; t < tagCount; t++) {
    display().fillRect(ax - 2, tags[t].y - 6, TAG_W, 12, tags[t].bg);
    ui::text(ax + 1, tags[t].y - 4, tags[t].text, 1, c.bg, tags[t].bg);
  }
}

// Time / weekday / date labels under the plot.
static void drawTimeRow(const Plot& p) {
  const ui::Palette& c = ui::pal();
  int16_t y = p.y + p.h + TIME_GAP;
  for (int i = 0; i < data.labels; i++) {
    int16_t w = ui::textWidth(data.labelText[i], 1);
    int16_t x = clampI(p.toX(data.labelFrac[i]) - w / 2, p.x, p.x + p.w - w);
    ui::text(x, y, data.labelText[i], 1, c.dim, c.bg);
  }
}

static void drawEmpty(const Plot& p) {
  const ui::Palette& c = ui::pal();
  int16_t cx = p.x + p.w / 2, cy = p.y + p.h / 2;
  ui::text(cx, cy - 18, data.empty1, 2, c.dim, c.bg, Align::Center);
  if (data.empty2[0]) ui::text(cx, cy + 4, data.empty2, 2, c.dim, c.bg, Align::Center);
}

// A line through data points, 2 px tall if `thick`.
static void drawSeries(const Plot& p, const float* frac, const float* values, int n, uint16_t color, bool thick) {
  int16_t prevX = p.toX(frac[0]), prevY = p.toY(values[0]);
  for (int i = 1; i < n; i++) {
    int16_t x = p.toX(frac[i]), y = p.toY(values[i]);
    ui::spanLine(prevX, prevY, x, y, color, thick ? 1 : 0);
    prevX = x;
    prevY = y;
  }
}

// Prices in at most 7 characters: 132.50, 1232.5, 12345
static void formatPrice(char* buf, size_t size, float v) {
  if (v >= 10000) snprintf(buf, size, "%.0f", v);
  else if (v >= 1000) snprintf(buf, size, "%.1f", v);
  else snprintf(buf, size, "%.2f", v);
}

void drawRangeChart(const SymbolState& s, const ChartBox& box, ChartRange range, const float* marks,
                    int markCount) {
  buildRange(s, range);
  const ui::Palette& c = ui::pal();
  Plot plot(box);
  if (data.n < 2) {
    drawEmpty(plot);
    return;
  }

  // ---- value range: the prices, the previous close (1D), and targets close to them ----
  float lo = data.price[0], hi = data.price[0];
  for (int i = 1; i < data.n; i++) {
    if (data.price[i] < lo) lo = data.price[i];
    if (data.price[i] > hi) hi = data.price[i];
  }
  float prev = s.quote.prevClose;
  bool prevLine = data.showPrevClose && prev > 0;
  if (prevLine) {
    if (prev < lo) lo = prev;
    if (prev > hi) hi = prev;
  }
  float dataLo = lo, dataHi = hi, near = hi - lo;
  for (int m = 0; m < markCount; m++) {   // within one chart-height of the prices: widen to fit
    if (marks[m] < lo && marks[m] >= dataLo - near) lo = marks[m];
    if (marks[m] > hi && marks[m] <= dataHi + near) hi = marks[m];
  }
  float values[MAX_AXIS_LABELS], step;
  int n = axisValues(plot, lo, hi, values, step);
  int decimals = labelDecimals(step, plot.hi);
  char texts[MAX_AXIS_LABELS][12];
  const char* textPtr[MAX_AXIS_LABELS];
  for (int i = 0; i < n; i++) {
    snprintf(texts[i], sizeof(texts[i]), "%.*f", decimals, values[i]);
    textPtr[i] = texts[i];
  }

  drawGrid(plot, values, n);
  Tag tags[3];
  int tagCount = 0;
  if (prevLine) {
    int16_t y = plot.toY(prev);
    ui::dashedHLine(plot.x, y, plot.w, c.dim, 4, 4);
    tags[tagCount].y = y;
    tags[tagCount].bg = c.dim;
    char p[10];
    formatPrice(p, sizeof(p), prev);
    snprintf(tags[tagCount].text, sizeof(tags[0].text), "P %s", p);
    tagCount++;
  }
  bool anyTarget = false;
  for (int m = 0; m < markCount && tagCount < 3; m++) {
    if (marks[m] < plot.lo || marks[m] > plot.hi) continue;
    int16_t y = plot.toY(marks[m]);
    ui::dashedHLine(plot.x, y, plot.w, c.target, 6, 4);
    tags[tagCount].y = y;
    tags[tagCount].bg = c.target;
    char p[10];
    formatPrice(p, sizeof(p), marks[m]);
    snprintf(tags[tagCount].text, sizeof(tags[0].text), "T %s", p);
    tagCount++;
    anyTarget = true;
  }

  // ---- the price line, 2 px, colored by the range's change ----
  float ref;
  float last = data.price[data.n - 1];
  bool up = rangeReference(s, range, ref) ? last >= ref : last >= data.price[0];
  drawSeries(plot, data.frac, data.price, data.n, up ? c.up : c.down, true);

  drawAxis(plot, values, n, textPtr, tags, tagCount);
  drawTimeRow(plot);

  // ---- legend row: what T and P mean; an older session's date on the right ----
  int16_t ly = plot.y + plot.h + LEGEND_GAP;
  int16_t x = plot.x;
  if (anyTarget) {
    display().fillRect(x, ly + 3, 10, 2, c.target);
    x = ui::text(x + 14, ly, "T = PRICE TARGET", 1, c.dim, c.bg) + 22;
  }
  if (prevLine) {
    ui::dashedHLine(x, ly + 3, 12, c.dim, 3, 3);
    ui::text(x + 16, ly, "P = PREVIOUS CLOSE", 1, c.dim, c.bg);
  }
  if (data.cornerTag[0]) {
    char tag[32];
    snprintf(tag, sizeof(tag), "SESSION OF %s", data.cornerTag);
    ui::text(plot.axisX() + TAG_W - 2, ly, tag, 1, c.target, c.bg, Align::Right);
  }
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

// ---------- compare vs SPY (8B) ----------

// Turns the chart points into % change from the range's reference price (the one the
// change under the price uses), or from the first point if there's no reference yet.
static bool toPercent(const SymbolState& s, ChartRange range, float* values, int n) {
  float ref;
  if (!rangeReference(s, range, ref)) ref = values[0];
  return rebaseToPercent(values, n, ref);
}

bool compareFigures(const SymbolState& s, const SymbolState& bench, ChartRange range, float& stockPct,
                    float& benchPct) {
  float refS, refB;
  if (!rangeReference(s, range, refS) || !rangeReference(bench, range, refB) || !bench.hasData) return false;
  stockPct = (s.quote.price / refS - 1.0f) * 100.0f;
  benchPct = (bench.quote.price / refB - 1.0f) * 100.0f;
  return true;
}

void drawCompareChart(const SymbolState& s, const SymbolState& bench, const ChartBox& box, ChartRange range) {
  // SPY first, its points put aside; then the stock, whose time labels the chart keeps.
  static float benchFrac[MAX_POINTS];
  static float benchPct[MAX_POINTS];
  buildRange(bench, range);
  int benchN = data.n;
  memcpy(benchFrac, data.frac, sizeof(float) * benchN);
  memcpy(benchPct, data.price, sizeof(float) * benchN);
  bool haveBench = benchN >= 2 && toPercent(bench, range, benchPct, benchN);
  buildRange(s, range);

  const ui::Palette& c = ui::pal();
  Plot plot(box);
  if (data.n < 2) {
    drawEmpty(plot);
    return;
  }
  toPercent(s, range, data.price, data.n);
  float last = data.price[data.n - 1];
  uint16_t color = last >= 0 ? c.up : c.down;

  // ---- legend row above the plot: "NVDA +1.24%  SPY +0.41%  ...  = +0.83% VS SPY" ----
  int16_t ly = plot.y - 16;
  char buf[48];
  float stockPct, spyPct;
  if (compareFigures(s, bench, range, stockPct, spyPct)) {
    display().fillRect(plot.x, ly + 3, 10, 3, color);
    snprintf(buf, sizeof(buf), "%s %+.2f%%", s.symbol, stockPct);
    int16_t x = ui::text(plot.x + 14, ly, buf, 1, color, c.bg) + 22;
    display().drawFastHLine(x, ly + 4, 10, benchColor());
    snprintf(buf, sizeof(buf), "%s %+.2f%%", bench.symbol, spyPct);
    ui::text(x + 14, ly, buf, 1, benchColor(), c.bg);
    float rel = stockPct - spyPct;
    snprintf(buf, sizeof(buf), "= %+.2f%% VS %s", rel, bench.symbol);
    ui::text(plot.x + plot.w, ly, buf, 1, rel >= 0 ? c.up : c.down, c.bg, Align::Right);
  } else {
    snprintf(buf, sizeof(buf), "%s VS %s: WAITING FOR %s DATA", s.symbol, bench.symbol, bench.symbol);
    ui::text(plot.x, ly, buf, 1, c.dim, c.bg);
  }

  // ---- % range covering both lines and 0 % ----
  float lo = 0, hi = 0;
  for (int i = 0; i < data.n; i++) {
    if (data.price[i] < lo) lo = data.price[i];
    if (data.price[i] > hi) hi = data.price[i];
  }
  for (int i = 0; haveBench && i < benchN; i++) {
    if (benchPct[i] < lo) lo = benchPct[i];
    if (benchPct[i] > hi) hi = benchPct[i];
  }
  float values[MAX_AXIS_LABELS], step;
  int n = axisValues(plot, lo, hi, values, step);
  int decimals = percentDecimals(step);
  char texts[MAX_AXIS_LABELS][12];
  const char* textPtr[MAX_AXIS_LABELS];
  for (int i = 0; i < n; i++) {
    if (fabsf(values[i]) < step / 2) snprintf(texts[i], sizeof(texts[i]), "0%%");
    else snprintf(texts[i], sizeof(texts[i]), "%+.*f%%", decimals, values[i]);
    textPtr[i] = texts[i];
  }

  drawGrid(plot, values, n);
  ui::dashedHLine(plot.x, plot.toY(0), plot.w, c.dim, 4, 4);   // 0 % = each one's reference price
  if (haveBench) drawSeries(plot, benchFrac, benchPct, benchN, benchColor(), false);   // SPY thin, underneath
  drawSeries(plot, data.frac, data.price, data.n, color, true);
  drawAxis(plot, values, n, textPtr, nullptr, 0);
  drawTimeRow(plot);
}
