#include "ui/splash_screen.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "hal/display.h"
#include "ui/fonts.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace splash {

using ui::Align;

// Layout (480 x 320), as on the design canvas
static const int16_t LOGO_X = 24, LOGO_Y = 24, LOGO_SIZE = 46;
static const int16_t ROW_X = 24, ROW_Y0 = 96, ROW_W = 250, ROW_H = 34, ROW_GAP = 8;
static const int16_t RING_CX = 380, RING_CY = 162, RING_R = 70, RING_T = 14;
static const int16_t CHART_TOP = 150, CHART_BOTTOM = 300;

// The faint background chart: a fixed rising line (0 = bottom, 1 = top), left to right.
static const float CHART[] = {0.30f, 0.36f, 0.33f, 0.42f, 0.40f, 0.47f, 0.44f, 0.52f, 0.50f, 0.58f, 0.55f, 0.53f,
                              0.61f, 0.66f, 0.62f, 0.70f, 0.68f, 0.75f, 0.72f, 0.80f, 0.78f, 0.86f, 0.83f, 0.92f};
static const int CHART_POINTS = sizeof(CHART) / sizeof(CHART[0]);

static char drawnRow[3][48];
static int drawnPriced = -1;

static int16_t chartY(float v) {
  return (int16_t)(CHART_BOTTOM - v * (CHART_BOTTOM - CHART_TOP));
}

static void drawBackgroundChart() {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  uint16_t line = ui::mix(p.bg, p.accent, 90);
  float step = (float)(cfg::SCREEN_W - 1) / (CHART_POINTS - 1);
  for (int16_t x = 0; x < cfg::SCREEN_W; x++) {   // the area under the line
    float pos = x / step;
    int i = (int)pos;
    if (i >= CHART_POINTS - 1) i = CHART_POINTS - 2;
    float v = CHART[i] + (CHART[i + 1] - CHART[i]) * (pos - i);
    int16_t y = chartY(v);
    gfx.drawFastVLine(x, y, cfg::SCREEN_H - y, p.accentSoft);
  }
  for (int i = 0; i + 1 < CHART_POINTS; i++) {
    ui::thickLine((int16_t)(i * step), chartY(CHART[i]), (int16_t)((i + 1) * step), chartY(CHART[i + 1]), line);
  }
}

// Rounded square with a rising trend arrow, like the design's logo mark.
static void drawLogo() {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  ui::fillRound(LOGO_X, LOGO_Y, LOGO_SIZE, LOGO_SIZE, 13, p.card);
  gfx.drawRoundRect(LOGO_X, LOGO_Y, LOGO_SIZE, LOGO_SIZE, 13, p.border);
  // the icon's 24-unit grid, scaled to 26 px and centered
  auto X = [](int u) { return (int16_t)(LOGO_X + 10 + u * 13 / 12); };
  auto Y = [](int u) { return (int16_t)(LOGO_Y + 10 + u * 13 / 12); };
  ui::thickLine(X(3), Y(17), X(9), Y(11), p.accent);
  ui::thickLine(X(9), Y(11), X(13), Y(15), p.accent);
  ui::thickLine(X(13), Y(15), X(21), Y(7), p.accent);
  ui::thickLine(X(15), Y(7), X(21), Y(7), p.accent);
  ui::thickLine(X(21), Y(7), X(21), Y(13), p.accent);
}

static void drawStepIcon(int16_t cx, int16_t cy, Step state) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  if (state == Step::Done) {
    gfx.fillCircle(cx, cy, 10, p.up);
    ui::checkMark(cx, cy - 1, p.bg);
  } else if (state == Step::Problem) {
    gfx.fillCircle(cx, cy, 10, p.down);
    gfx.fillRect(cx - 1, cy - 6, 3, 8, p.bg);   // "!"
    gfx.fillRect(cx - 1, cy + 4, 3, 3, p.bg);
  } else {
    gfx.drawCircle(cx, cy, 10, p.accent);
    gfx.drawCircle(cx, cy, 9, p.accent);
    for (int d = -4; d <= 4; d += 4) gfx.fillCircle(cx + d, cy, 1, p.accent);
  }
}

static void drawRow(int i, const char* title, Step state, const char* detail) {
  char key[sizeof(drawnRow[0])];
  snprintf(key, sizeof(key), "%d|%s", (int)state, detail);
  if (strcmp(key, drawnRow[i]) == 0) return;
  strcpy(drawnRow[i], key);

  const ui::Palette& p = ui::pal();
  int16_t y = ROW_Y0 + i * (ROW_H + ROW_GAP);
  ui::fillRound(ROW_X, y, ROW_W, ROW_H, 10, p.card);
  drawStepIcon(ROW_X + 20, y + ROW_H / 2, state);
  int16_t afterTitle = ui::textMid(ROW_X + 40, y, ROW_H, InterSemi12, p.text, title);
  char shown[40];
  ui::fitText(Inter10, detail, ROW_X + ROW_W - 10 - (afterTitle + 12), shown, sizeof(shown));
  ui::textMid(ROW_X + ROW_W - 10, y, ROW_H, Inter10, state == Step::Problem ? p.down : p.sub, shown, Align::Right);
}

static void drawRing(int priced, int total) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  // Row by row: each pixel of the ring is lit if its angle (clockwise from the top) is within
  // the done fraction; runs of the same color become one fillRect.
  float litAngle = total > 0 ? 2.0f * (float)M_PI * priced / total : 0;
  const int outer2 = RING_R * RING_R, inner2 = (RING_R - RING_T) * (RING_R - RING_T);
  for (int16_t dy = -RING_R; dy <= RING_R; dy++) {
    int16_t runStart = 0;
    uint16_t runColor = 0;
    bool inRun = false;
    for (int16_t dx = -RING_R; dx <= RING_R + 1; dx++) {
      int d2 = dx * dx + dy * dy;
      bool in = dx <= RING_R && d2 <= outer2 && d2 >= inner2;
      uint16_t color = 0;
      if (in) {
        float a = atan2f((float)dx, (float)-dy);
        if (a < 0) a += 2.0f * (float)M_PI;
        color = a < litAngle ? p.accent : p.card;
      }
      if (inRun && (!in || color != runColor)) {
        gfx.fillRect(RING_CX + runStart, RING_CY + dy, dx - runStart, 1, runColor);
        inRun = false;
      }
      if (in && !inRun) {
        runStart = dx;
        runColor = color;
        inRun = true;
      }
    }
  }
  gfx.fillCircle(RING_CX, RING_CY, RING_R - RING_T - 1, p.bg);
  char count[12];
  snprintf(count, sizeof(count), "%d/%d", priced, total);
  ui::text(RING_CX, RING_CY + 6, Mono30, p.text, count, Align::Center);
  ui::text(RING_CX, RING_CY + 26, Inter10, p.sub, "prices in", Align::Center);
}

void drawFull() {
  const ui::Palette& p = ui::pal();
  display().fillScreen(p.bg);
  drawBackgroundChart();
  drawLogo();
  ui::text(LOGO_X + LOGO_SIZE + 12, LOGO_Y + 22, InterBold24, p.text, "Market Tracker");
  ui::text(LOGO_X + LOGO_SIZE + 12, LOGO_Y + 40, Inter10, p.sub, "by Lucas Tinter & Brandon Fulk");
  char footer[48];
  snprintf(footer, sizeof(footer), "firmware %s " UI_DOT " tap to skip", cfg::FIRMWARE_VERSION);
  ui::text(LOGO_X, 306, Inter10, p.faint, footer);
  for (auto& row : drawnRow) row[0] = '\0';
  drawnPriced = -1;
}

void update(const Boot& boot) {
  drawRow(0, "Wi-Fi", boot.wifi, boot.wifiDetail);
  drawRow(1, "Clock", boot.clock, boot.clockDetail);
  char prices[20];
  snprintf(prices, sizeof(prices), "%d of %d", boot.priced, boot.total);
  drawRow(2, "Prices", boot.priced >= boot.total ? Step::Done : Step::Waiting, prices);
  if (boot.priced != drawnPriced) {
    drawRing(boot.priced, boot.total);
    drawnPriced = boot.priced;
  }
}

}  // namespace splash
