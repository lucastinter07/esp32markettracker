#include "ui/gfx.h"
#include <string.h>
#include "hal/display.h"

namespace ui {

static const char ELLIPSIS = 0x7F;   // the "…" glyph in the Inter fonts (see ui/fonts.h)

static const GFXglyph* glyphOf(const GFXfont& f, char c) {
  uint8_t code = (uint8_t)c;
  if (code < f.first || code > f.last) return nullptr;
  return &f.glyph[code - f.first];
}

int16_t textWidth(const GFXfont& f, const char* s) {
  int16_t w = 0;
  for (; *s; s++) {
    const GFXglyph* g = glyphOf(f, *s);
    if (g) w += g->xAdvance;
  }
  return w;
}

int16_t capHeight(const GFXfont& f) {
  const GFXglyph* g = glyphOf(f, 'H');
  if (!g) g = glyphOf(f, '0');
  return g ? (int16_t)-g->yOffset : 8;
}

// One glyph, row by row; each run of set pixels is one fillRect.
static void drawGlyph(DFRobot_GDL& gfx, int16_t x, int16_t y, const GFXfont& f, const GFXglyph& g, uint16_t color) {
  const uint8_t* bitmap = f.bitmap + g.bitmapOffset;
  uint32_t bit = 0;
  int16_t left = x + g.xOffset, top = y + g.yOffset;
  for (int16_t row = 0; row < g.height; row++) {
    int16_t runStart = -1;
    for (int16_t col = 0; col < g.width; col++, bit++) {
      bool on = bitmap[bit >> 3] & (0x80 >> (bit & 7));
      if (on && runStart < 0) runStart = col;
      if (!on && runStart >= 0) {
        gfx.fillRect(left + runStart, top + row, col - runStart, 1, color);
        runStart = -1;
      }
    }
    if (runStart >= 0) gfx.fillRect(left + runStart, top + row, g.width - runStart, 1, color);
  }
}

int16_t text(int16_t x, int16_t y, const GFXfont& f, uint16_t color, const char* s, Align a) {
  if (a != Align::Left) {
    int16_t w = textWidth(f, s);
    x -= a == Align::Center ? w / 2 : w;
  }
  DFRobot_GDL& gfx = display();
  for (; *s; s++) {
    const GFXglyph* g = glyphOf(f, *s);
    if (!g) continue;
    if (g->width && g->height) drawGlyph(gfx, x, y, f, *g, color);
    x += g->xAdvance;
  }
  return x;
}

int16_t textMid(int16_t x, int16_t y, int16_t h, const GFXfont& f, uint16_t color, const char* s, Align a) {
  return text(x, y + (h + capHeight(f)) / 2, f, color, s, a);
}

void fitText(const GFXfont& f, const char* s, int16_t maxW, char* out, size_t size) {
  if (size == 0) return;
  size_t len = strlen(s);
  if (textWidth(f, s) <= maxW) {
    strncpy(out, s, size - 1);
    out[size - 1] = '\0';
    return;
  }
  bool hasEllipsis = f.last >= (uint8_t)ELLIPSIS;
  const char* tail = hasEllipsis ? "\x7f" : "..";
  int16_t room = maxW - textWidth(f, tail);
  size_t keep = 0;
  int16_t w = 0;
  while (keep < len && keep + 3 < size) {
    const GFXglyph* g = glyphOf(f, s[keep]);
    int16_t adv = g ? g->xAdvance : 0;
    if (w + adv > room) break;
    w += adv;
    keep++;
  }
  while (keep > 0 && s[keep - 1] == ' ') keep--;   // no space before the ellipsis
  memcpy(out, s, keep);
  strcpy(out + keep, tail);
}

uint16_t mix(uint16_t from, uint16_t to, uint8_t amount) {
  int r0 = from >> 11, g0 = (from >> 5) & 0x3F, b0 = from & 0x1F;
  int r1 = to >> 11, g1 = (to >> 5) & 0x3F, b1 = to & 0x1F;
  int r = r0 + (r1 - r0) * amount / 255;
  int g = g0 + (g1 - g0) * amount / 255;
  int b = b0 + (b1 - b0) * amount / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void fillRound(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  int16_t maxR = (w < h ? w : h) / 2;
  if (r > maxR) r = maxR;
  if (r <= 0) display().fillRect(x, y, w, h, color);
  else display().fillRoundRect(x, y, w, h, r, color);
}

void pill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  fillRound(x, y, w, h, h / 2, color);
}

void dashedHLine(int16_t x, int16_t y, int16_t w, int16_t dash, int16_t gap, uint16_t color) {
  DFRobot_GDL& gfx = display();
  for (int16_t dx = 0; dx < w; dx += dash + gap) {
    gfx.drawFastHLine(x + dx, y, (dx + dash <= w) ? dash : w - dx, color);
  }
}

void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  DFRobot_GDL& gfx = display();
  gfx.drawLine(x0, y0, x1, y1, color);
  gfx.drawLine(x0, y0 + 1, x1, y1 + 1, color);
}

void checkMark(int16_t cx, int16_t cy, uint16_t color) {
  thickLine(cx - 4, cy, cx - 1, cy + 3, color);
  thickLine(cx - 1, cy + 3, cx + 4, cy - 3, color);
}

void chevron(int16_t cx, int16_t cy, bool left, uint16_t color) {
  int16_t d = left ? 1 : -1;
  thickLine(cx + 2 * d, cy - 5, cx - 3 * d, cy, color);
  thickLine(cx - 3 * d, cy, cx + 2 * d, cy + 5, color);
}

void triangle(int16_t cx, int16_t cy, int16_t size, bool up, uint16_t color) {
  int16_t h = size / 2;
  if (up) display().fillTriangle(cx - h, cy + h, cx + h, cy + h, cx, cy - h, color);
  else display().fillTriangle(cx - h, cy - h, cx + h, cy - h, cx, cy + h, color);
}

}  // namespace ui
