#include "ui/gfx.h"
#include <string.h>
#include "config.h"
#include "hal/display.h"
#include "ui/font6x8.h"
#include "ui/palette.h"

namespace ui {

// Up to 4 full-width rows of pixels, 3 bytes each (the panel's RGB666 format).
static uint8_t strip[cfg::SCREEN_W * 3 * 4];

// RGB565 -> the 3 bytes the panel takes, exactly as the library's fillRect sends them, so
// a text box and a filled area of the same color match to the bit.
static void toPanel(uint16_t c, uint8_t out[3]) {
  out[0] = (uint8_t)((c >> 8) & 0xF8);
  out[1] = (uint8_t)((c >> 3) & 0xFC);
  out[2] = (uint8_t)(c << 3);
}

static bool pixelOn(const char* s, int16_t cx, int16_t row, uint8_t size) {
  int16_t cell = CHAR_W * size;
  int16_t col = (cx % cell) / size;   // 0..5; column 5 is the gap between characters
  if (col >= 5) return false;
  uint8_t glyph = (uint8_t)s[cx / cell];
  return (FONT6X8[glyph * 5 + col] >> row) & 1;
}

// Slower path (cfg::TEXT_BLOCK_WRITES = false): the box, then each glyph column's runs.
static void drawWithRects(int16_t x, int16_t y, const char* s, size_t n, uint8_t size, uint16_t fg, uint16_t bg) {
  DFRobot_GDL& gfx = display();
  gfx.fillRect(x, y, (int16_t)(n * CHAR_W * size), CHAR_H * size, bg);
  for (size_t i = 0; i < n; i++) {
    const uint8_t* g = &FONT6X8[(uint8_t)s[i] * 5];
    for (int16_t col = 0; col < 5; col++) {
      int16_t row = 0;
      while (row < CHAR_H) {
        if (!((g[col] >> row) & 1)) {
          row++;
          continue;
        }
        int16_t start = row;
        while (row < CHAR_H && ((g[col] >> row) & 1)) row++;
        gfx.fillRect(x + (int16_t)(i * CHAR_W + col) * size, y + start * size, size, (row - start) * size, fg);
      }
    }
  }
}

int16_t textWidth(const char* s, uint8_t size) {
  return (int16_t)(strlen(s) * CHAR_W * (size ? size : 1));
}

int16_t text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t fg, uint16_t bg, Align a) {
  if (size == 0) size = 1;
  size_t n = strlen(s);
  int16_t w = (int16_t)(n * CHAR_W * size), h = CHAR_H * size;
  if (a == Align::Right) x -= w;
  else if (a == Align::Center) x -= w / 2;
  int16_t end = x + w;

  // The part that's on screen
  int16_t x0 = x < 0 ? 0 : x, x1 = end > cfg::SCREEN_W ? cfg::SCREEN_W : end;
  int16_t y0 = y < 0 ? 0 : y, y1 = y + h > cfg::SCREEN_H ? cfg::SCREEN_H : y + h;
  if (n == 0 || x0 >= x1 || y0 >= y1) return end;
  if (!cfg::TEXT_BLOCK_WRITES) {
    drawWithRects(x, y, s, n, size, fg, bg);
    return end;
  }

  uint8_t on[3], off[3];
  toPanel(fg, on);
  toPanel(bg, off);
  int16_t vw = x1 - x0;
  int16_t rowsPerBlock = (int16_t)(sizeof(strip) / ((size_t)vw * 3));
  for (int16_t by = y0; by < y1; by += rowsPerBlock) {
    int16_t rows = y1 - by < rowsPerBlock ? y1 - by : rowsPerBlock;
    uint8_t* p = strip;
    for (int16_t r = 0; r < rows; r++) {
      int16_t glyphRow = (by + r - y) / size;
      for (int16_t px = x0; px < x1; px++) {
        const uint8_t* c = pixelOn(s, px - x, glyphRow, size) ? on : off;
        *p++ = c[0];
        *p++ = c[1];
        *p++ = c[2];
      }
    }
    displayPushPixels(x0, by, vw, rows, strip);
  }
  return end;
}

void fitText(const char* s, uint8_t size, int16_t maxW, char* out, size_t outSize) {
  if (outSize == 0) return;
  size_t fit = maxW > 0 ? (size_t)(maxW / (CHAR_W * (size ? size : 1))) : 0;
  size_t n = strlen(s);
  if (fit >= outSize) fit = outSize - 1;
  if (n <= fit) {
    memcpy(out, s, n + 1);
    return;
  }
  memcpy(out, s, fit);
  out[fit] = '\0';
  if (fit > 0) out[fit - 1] = '.';
}

void headerBar(const char* left, const char* right, int16_t y) {
  const Palette& p = pal();
  display().fillRect(0, y, cfg::SCREEN_W, BAR_H, p.bar);
  int16_t ty = y + (BAR_H - 2 * CHAR_H) / 2;
  if (left && left[0]) text(6, ty, left, 2, p.ink, p.bar);
  if (right && right[0]) text(cfg::SCREEN_W - 6, ty, right, 2, p.ink, p.bar, Align::Right);
}

void listRow(int16_t y, const char* label, const char* value, uint16_t valueColor, bool highlighted, bool leaders) {
  DFRobot_GDL& gfx = display();
  const Palette& p = pal();
  uint16_t bg = highlighted ? p.hi : p.bg;
  gfx.fillRect(0, y, cfg::SCREEN_W, LISTROW_H, bg);
  if (highlighted) gfx.fillRect(0, y, 3, LISTROW_H, p.stripe);
  int16_t x = text(8, y + 7, label, 2, p.text, bg);
  int16_t right = cfg::SCREEN_W - 8 - textWidth(value, 2);
  int dots = (right - 8 - (x + 6)) / CHAR_W;
  if (leaders && dots > 0) {
    char row[80];
    if (dots > (int)sizeof(row) - 1) dots = sizeof(row) - 1;
    memset(row, '.', dots);
    row[dots] = '\0';
    text(x + 6, y + 11, row, 1, mix(p.line, p.dim, 80), bg);
  }
  text(cfg::SCREEN_W - 8, y + 7, value, 2, valueColor, bg, Align::Right);
}

void button(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint8_t size, uint16_t color, bool filled,
            bool highlighted) {
  DFRobot_GDL& gfx = display();
  const Palette& p = pal();
  uint16_t bg = filled ? p.accent : (highlighted ? p.hi : p.bg);
  gfx.fillRect(x, y, w, h, bg);
  gfx.drawRect(x, y, w, h, filled ? p.accent : color);
  if (filled && highlighted) gfx.drawRect(x + 3, y + 3, w - 6, h - 6, p.accentInk);   // the cursor is on it
  text(x + w / 2, y + (h - CHAR_H * size) / 2, label, size, filled ? p.accentInk : color, bg, Align::Center);
}

void dashedHLine(int16_t x, int16_t y, int16_t w, uint16_t color, int16_t dash, int16_t gap) {
  DFRobot_GDL& gfx = display();
  for (int16_t dx = 0; dx < w; dx += dash + gap) {
    gfx.drawFastHLine(x + dx, y, (w - dx) < dash ? (w - dx) : dash, color);
  }
}

void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  DFRobot_GDL& gfx = display();
  gfx.drawLine(x0, y0, x1, y1, color);
  gfx.drawLine(x0, y0 + 1, x1, y1 + 1, color);
}

uint16_t mix(uint16_t from, uint16_t to, uint8_t amount) {
  auto ch = [&](int shift, int bits) {
    int mask = (1 << bits) - 1;
    int a = (from >> shift) & mask, b = (to >> shift) & mask;
    return (uint16_t)(((a + (b - a) * amount / 255) & mask) << shift);
  };
  return ch(11, 5) | ch(5, 6) | ch(0, 5);
}

}  // namespace ui
