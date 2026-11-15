#include "ui/splash_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "hal/display.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace splash {

using ui::Align;

// Layout (480 x 320), as on the design canvas ("★ FINAL · Layout A · Graphite")
static const int16_t LOGO_X = 16, LOGO_Y = 14, LOGO_W = 60;
static const int16_t TITLE_X = 90;
static const int16_t CHART_BOTTOM = 232;          // the dotted fill stops above this
static const int16_t RULE_Y = 236;
static const int16_t LOG_X = 16, LOG_Y0 = 248, LOG_PITCH = 22;
static const int16_t PCT_RIGHT = 464, PCT_Y = 246;
static const int16_t STEPS_X = 320, STEPS_Y = 296, STEPS_W = 147, STEP_H = 10;

// The rising chart: y of each point, x spread evenly over the full width (from the mockup).
static const int16_t CHART_Y[] = {226, 220, 216, 211, 202, 196, 195, 190, 199, 195, 194, 187, 188, 179, 177, 180,
                                  186, 181, 176, 170, 165, 173, 179, 171, 165, 162, 158, 161, 156, 154, 145, 137,
                                  129, 134, 136, 143, 141, 136, 133, 136, 139, 131, 123, 121, 125, 118, 110, 115};
static const int CHART_POINTS = sizeof(CHART_Y) / sizeof(CHART_Y[0]);

static char drawnLine[3][16];
static int drawnPriced = -1, drawnTotal = -1;

static int16_t chartX(int i) {
  return (int16_t)((i * (cfg::SCREEN_W - 1) * 2 + (CHART_POINTS - 1)) / (2 * (CHART_POINTS - 1)));   // rounded
}

static void drawChart() {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  // A dot every 4 px under the line
  for (int16_t x = 0; x < cfg::SCREEN_W; x += 4) {
    int i = x * (CHART_POINTS - 1) / (cfg::SCREEN_W - 1);
    if (i > CHART_POINTS - 2) i = CHART_POINTS - 2;
    int16_t xa = chartX(i), xb = chartX(i + 1);
    float f = (float)(x - xa) / (float)(xb - xa > 0 ? xb - xa : 1);
    int16_t yLine = (int16_t)lroundf(CHART_Y[i] + (CHART_Y[i + 1] - CHART_Y[i]) * f);
    for (int16_t y = yLine + 4; y < CHART_BOTTOM; y += 4) gfx.drawPixel(x, y, p.edge);
  }
  for (int i = 0; i + 1 < CHART_POINTS; i++) {
    ui::thickLine(chartX(i), CHART_Y[i], chartX(i + 1), CHART_Y[i + 1], p.up);
  }
  int last = CHART_POINTS - 1;
  gfx.fillRect(chartX(last) - 4, CHART_Y[last] - 3, 6, 6, p.text);   // "now"
}

// Framed candlestick chart with a rising trend line, drawn on a 104-unit grid scaled to w.
static void drawLogo(int16_t x, int16_t y, int16_t w) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  auto u = [w](int units) { return (int16_t)((units * w * 2 + 104) / 208); };   // rounded
  gfx.fillRect(x, y, w, w, p.panel);
  gfx.drawRect(x, y, w, w, p.edge);
  gfx.drawRect(x + 1, y + 1, w - 2, w - 2, p.edge);
  struct Candle { int16_t x, top, bottom, open, close; };
  static const Candle CANDLES[] = {{16, 70, 88, 84, 74}, {34, 56, 80, 76, 62}, {52, 50, 72, 56, 66},
                                   {70, 34, 60, 58, 40}, {88, 20, 46, 44, 26}};
  for (const Candle& c : CANDLES) {
    uint16_t color = c.close <= c.open ? p.up : p.down;   // y grows downward: lower = higher price
    int16_t cx = x + u(c.x);
    int16_t wickW = u(2) > 1 ? u(2) : 1;
    gfx.fillRect(cx + u(4), y + u(c.top), wickW, u(c.bottom - c.top), color);
    int16_t bodyTop = c.open < c.close ? c.open : c.close;
    int16_t bodyH = u(abs(c.open - c.close));
    gfx.fillRect(cx, y + u(bodyTop), u(10), bodyH > 2 ? bodyH : 2, color);
  }
  static const int16_t TREND[][2] = {{10, 92}, {38, 70}, {58, 76}, {76, 46}, {96, 16}};
  for (int i = 0; i + 1 < 5; i++) {
    ui::thickLine(x + u(TREND[i][0]), y + u(TREND[i][1]), x + u(TREND[i + 1][0]), y + u(TREND[i + 1][1]), p.text);
  }
}

// One boot-log line: "> WIFI.... OK"
static void drawLine(int i, const char* name, const char* state, uint16_t stateColor) {
  if (strcmp(drawnLine[i], state) == 0) return;
  snprintf(drawnLine[i], sizeof(drawnLine[i]), "%s", state);
  const ui::Palette& p = ui::pal();
  int16_t y = LOG_Y0 + i * LOG_PITCH;
  char head[16];
  snprintf(head, sizeof(head), "> %s", name);
  int16_t x = ui::text(LOG_X, y, head, 2, p.dim, p.bg) + 12;
  display().fillRect(x, y, 300 - x, 16, p.bg);
  ui::text(x, y, state, 2, stateColor, p.bg);
}

static void drawProgress(int priced, int total) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  int pct = total > 0 ? (priced * 100 + total / 2) / total : 0;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  gfx.fillRect(PCT_RIGHT - 4 * 30, PCT_Y, 4 * 30, 40, p.bg);   // room for "100%" at size 5
  ui::text(PCT_RIGHT, PCT_Y, buf, 5, p.text, p.bg, Align::Right);
  if (total <= 0) return;
  int16_t pitch = STEPS_W / total;
  for (int k = 0; k < total; k++) {
    gfx.fillRect(STEPS_X + k * pitch, STEPS_Y, pitch - 3, STEP_H, k < priced ? p.up : p.edge);
  }
}

void drawFull() {
  const ui::Palette& p = ui::pal();
  display().fillScreen(p.bg);
  drawLogo(LOGO_X, LOGO_Y, LOGO_W);
  ui::text(TITLE_X, 22, "MARKET TRACKER", 3, p.text, p.bg);
  ui::text(TITLE_X, 52, "BY LUCAS TINTER & BRANDON FULK", 1, p.dim, p.bg);
  drawChart();
  display().drawFastHLine(0, RULE_Y, cfg::SCREEN_W, p.line);
  for (auto& line : drawnLine) line[0] = '\0';
  drawnPriced = drawnTotal = -1;
}

void update(const Boot& boot) {
  const ui::Palette& p = ui::pal();
  auto stepText = [](Step s, const char* problem) { return s == Step::Done ? "OK" : (s == Step::Problem ? problem : "..."); };
  auto stepColor = [&p](Step s) { return s == Step::Done ? p.up : (s == Step::Problem ? p.down : p.text); };
  drawLine(0, "WIFI....", stepText(boot.wifi, "SETUP"), stepColor(boot.wifi));
  drawLine(1, "CLOCK...", stepText(boot.clock, "--"), stepColor(boot.clock));
  char prices[12];
  snprintf(prices, sizeof(prices), "%d/%d", boot.priced, boot.total);
  drawLine(2, "PRICES..", prices, boot.priced >= boot.total ? p.up : p.text);
  if (boot.priced != drawnPriced || boot.total != drawnTotal) {
    drawProgress(boot.priced, boot.total);
    drawnPriced = boot.priced;
    drawnTotal = boot.total;
  }
}

}  // namespace splash
