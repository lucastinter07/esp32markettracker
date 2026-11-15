// PC stand-in for the display library, for tools/preview: the drawing calls the redesigned
// screens use, painting into a 480 x 320 RGB565 framebuffer that preview.cpp saves as an image.
// Only what Phase 9 screens need; the old built-in-font calls are accepted and ignored.
#pragma once
#include <stdint.h>
#include <stdlib.h>

#define PROGMEM

typedef struct {
  uint16_t bitmapOffset;
  uint8_t width, height;
  uint8_t xAdvance;
  int8_t xOffset, yOffset;
} GFXglyph;

typedef struct {
  uint8_t* bitmap;
  GFXglyph* glyph;
  uint8_t first, last;
  uint8_t yAdvance;
} GFXfont;

class DFRobot_GDL {
 public:
  static const int16_t W = 480, H = 320;
  uint16_t fb[W * H];

  void drawPixel(int16_t x, int16_t y, uint16_t c) {
    if (x >= 0 && y >= 0 && x < W && y < H) fb[y * W + x] = c;
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    for (int16_t j = 0; j < h; j++)
      for (int16_t i = 0; i < w; i++) drawPixel(x + i, y + j, c);
  }
  void fillScreen(uint16_t c) { fillRect(0, 0, W, H, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    drawFastHLine(x, y, w, c);
    drawFastHLine(x, y + h - 1, w, c);
    drawFastVLine(x, y, h, c);
    drawFastVLine(x + w - 1, y, h, c);
  }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
      drawPixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
  void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t c) {
    for (int16_t y = -r; y <= r; y++)
      for (int16_t x = -r; x <= r; x++)
        if (x * x + y * y <= r * r + r) drawPixel(cx + x, cy + y, c);
  }
  void drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t c) {
    for (int16_t y = -r; y <= r; y++)
      for (int16_t x = -r; x <= r; x++) {
        int d = x * x + y * y;
        if (d <= r * r + r && d > (r - 1) * (r - 1) + (r - 1)) drawPixel(cx + x, cy + y, c);
      }
  }
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    for (int16_t j = 0; j < h; j++)
      for (int16_t i = 0; i < w; i++) {
        int16_t dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
        int16_t dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        if (dx * dx + dy * dy <= r * r) drawPixel(x + i, y + j, c);
      }
  }
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    for (int16_t j = 0; j < h; j++)
      for (int16_t i = 0; i < w; i++) {
        int16_t dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
        int16_t dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        int d = dx * dx + dy * dy;
        bool inside = d <= r * r;
        bool edge = i == 0 || j == 0 || i == w - 1 || j == h - 1 || (d > (r - 1) * (r - 1) && dx && dy);
        if (inside && edge) drawPixel(x + i, y + j, c);
      }
  }
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) {
    int minX = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2), maxX = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    for (int y = minY; y <= maxY; y++)
      for (int x = minX; x <= maxX; x++) {
        long a = (long)(x1 - x0) * (y - y0) - (long)(y1 - y0) * (x - x0);
        long b = (long)(x2 - x1) * (y - y1) - (long)(y2 - y1) * (x - x1);
        long d = (long)(x0 - x2) * (y - y2) - (long)(y0 - y2) * (x - x2);
        if ((a >= 0 && b >= 0 && d >= 0) || (a <= 0 && b <= 0 && d <= 0)) drawPixel(x, y, c);
      }
  }
  // Built-in font calls from screens not yet redesigned: accepted, not drawn.
  void setTextSize(uint8_t) {}
  void setTextColor(uint16_t, uint16_t = 0) {}
  void setCursor(int16_t, int16_t) {}
  void print(const char*) {}
};
