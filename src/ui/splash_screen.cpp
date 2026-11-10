#include "ui/splash_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "hal/display.h"
#include "ui/theme.h"

namespace splash {

// The emblem: a small framed chart with four candlesticks and a rising trend line.
static const int16_t LOGO_X = 180, LOGO_Y = 26, LOGO_W = 120, LOGO_H = 74;

static const int16_t STATUS_Y = 272;
static const int16_t BAR_X = 140, BAR_Y = 288, BAR_W = 200, BAR_H = 6;

static char drawnStatus[40];
static int drawnPercent = -1;

// Text of the given size, centered horizontally at row y.
static void printCentered(const char* text, int16_t y, uint8_t size, uint16_t color) {
  DFRobot_GDL& gfx = display();
  gfx.setTextSize(size);
  gfx.setTextColor(color, theme::BG);
  gfx.setCursor((cfg::SCREEN_W - (int16_t)strlen(text) * theme::CHAR_W * size) / 2, y);
  gfx.print(text);
}

// A candlestick: thin wick from high to low, body from open to close.
static void candle(int16_t x, int16_t high, int16_t low, int16_t open, int16_t close) {
  DFRobot_GDL& gfx = display();
  uint16_t color = close <= open ? theme::UP : theme::DOWN;   // screen y grows downward
  gfx.drawFastVLine(x + 4, high, low - high, color);
  int16_t top = min(open, close);
  gfx.fillRect(x, top, 9, max(abs(close - open), 2), color);
}

static void drawLogo() {
  DFRobot_GDL& gfx = display();
  gfx.drawRoundRect(LOGO_X, LOGO_Y, LOGO_W, LOGO_H, 8, theme::FRAME);
  for (int i = 1; i < 3; i++) {   // faint gridlines
    gfx.drawFastHLine(LOGO_X + 6, LOGO_Y + i * LOGO_H / 3, LOGO_W - 12, theme::GRID);
  }

  // Candles step up from left to right (one red pullback).
  int16_t bx = LOGO_X + 14, by = LOGO_Y;
  candle(bx,      by + 46, by + 66, by + 62, by + 50);
  candle(bx + 22, by + 36, by + 58, by + 50, by + 40);
  candle(bx + 44, by + 34, by + 54, by + 38, by + 48);   // the red one
  candle(bx + 66, by + 20, by + 46, by + 44, by + 26);

  // Amber trend line (3 px thick) ending in an arrowhead at the top right.
  const int16_t px[] = {LOGO_X + 10, LOGO_X + 40, LOGO_X + 62, LOGO_X + 82, LOGO_X + 104};
  const int16_t py[] = {LOGO_Y + 58, LOGO_Y + 42, LOGO_Y + 48, LOGO_Y + 26, LOGO_Y + 14};
  for (int i = 0; i < 4; i++) {
    for (int d = -1; d <= 1; d++) {
      gfx.drawLine(px[i], py[i] + d, px[i + 1], py[i + 1] + d, theme::ACCENT);
    }
  }
  int16_t tx = px[4], ty = py[4];
  gfx.fillTriangle(tx + 6, ty - 5, tx - 6, ty - 2, tx + 1, ty + 9, theme::ACCENT);
}

void drawFull() {
  DFRobot_GDL& gfx = display();
  gfx.fillScreen(theme::BG);
  drawLogo();

  // "MARKET TRACKER" in size 4 (24 px per character), two colors
  const int16_t titleY = 118;
  int16_t titleX = (cfg::SCREEN_W - 14 * 24) / 2;
  gfx.setTextSize(4);
  gfx.setTextColor(theme::TEXT, theme::BG);
  gfx.setCursor(titleX, titleY);
  gfx.print("MARKET");
  gfx.setTextColor(theme::ACCENT, theme::BG);
  gfx.setCursor(titleX + 7 * 24, titleY);
  gfx.print("TRACKER");
  printCentered("ESP32-S3 live market terminal", 160, 1, theme::LABEL);

  gfx.drawFastHLine(160, 186, 160, theme::FRAME);
  printCentered("CREATED BY", 200, 1, theme::STAT_LABEL);
  printCentered("Lucas Tinter & Brandon Fulk", 216, 2, theme::TEXT);

  gfx.drawRect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, theme::FRAME);
  printCentered("tap or press any button to skip", 306, 1, theme::DIM);
  drawnStatus[0] = '\0';
  drawnPercent = -1;
}

void update(const char* status, int percent) {
  DFRobot_GDL& gfx = display();
  if (strcmp(status, drawnStatus) != 0) {
    gfx.fillRect(0, STATUS_Y, cfg::SCREEN_W, 8, theme::BG);
    printCentered(status, STATUS_Y, 1, theme::LABEL);
    strncpy(drawnStatus, status, sizeof(drawnStatus) - 1);
    drawnStatus[sizeof(drawnStatus) - 1] = '\0';
  }
  percent = constrain(percent, 0, 100);
  if (percent != drawnPercent) {
    int16_t filled = BAR_W * percent / 100;
    gfx.fillRect(BAR_X, BAR_Y, filled, BAR_H, theme::UP);
    gfx.fillRect(BAR_X + filled, BAR_Y, BAR_W - filled, BAR_H, theme::BG);
    drawnPercent = percent;
  }
}

}  // namespace splash
