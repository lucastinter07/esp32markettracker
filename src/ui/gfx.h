// Drawing helpers for the Phase 9 screens (layout A): text in the display's built-in 6 x 8
// font, header bars, dashed and 2 px lines.
//
// Text: (x, y) is the TOP-LEFT of the first character cell (like setCursor), or its top-right
// / top-center with Align::Right / Center. Each character is a 6 x 8 cell scaled by `size`
// (size 2 = 12 x 16 px). The cells are painted opaque in `bg`, so pass the color that is
// actually behind the text. A string goes to the panel in a few block writes instead of the
// library's pixel-by-pixel print(), and never moves the library's text cursor.
//
// Special glyphs from the font's upper half, as separate string literals so a following
// letter or digit isn't read as part of the escape:  "OPEN " G_DOT " 2H 14M"
#pragma once
#include <stddef.h>
#include <stdint.h>

#define G_DIAMOND "\x04"   // the price-target alert banner
#define G_UP      "\x1E"   // up triangle
#define G_DOWN    "\x1F"   // down triangle
#define G_MENU    "\xF0"   // three bars (opens Settings)
#define G_DOT     "\xFA"   // middle dot, a separator
#define G_SQUARE  "\xFE"   // small filled square (chart legends)

namespace ui {

enum class Align : uint8_t { Left, Center, Right };

constexpr int16_t CHAR_W = 6;   // one character cell at size 1
constexpr int16_t CHAR_H = 8;
constexpr int16_t BAR_H = 24;   // header bar height (text size 2)

int16_t textWidth(const char* s, uint8_t size);   // pixels: characters x 6 x size

// Draws s and returns the x just after its right edge.
int16_t text(int16_t x, int16_t y, const char* s, uint8_t size, uint16_t fg, uint16_t bg,
             Align a = Align::Left);

// Copies s into out, cut to at most maxW pixels at this size (the last character becomes '.'
// when something was cut).
void fitText(const char* s, uint8_t size, int16_t maxW, char* out, size_t outSize);

// A header bar across the screen (y .. y+BAR_H) in the theme's bar color, with size-2 text
// in its ink color: `left` from x 6, `right` (optional) right-aligned at x 474.
void headerBar(const char* left, const char* right, int16_t y = 0);

void dashedHLine(int16_t x, int16_t y, int16_t w, uint16_t color, int16_t dash = 4, int16_t gap = 4);
void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);   // 2 px tall

// `from` blended toward `to` by amount/255 (RGB565).
uint16_t mix(uint16_t from, uint16_t to, uint8_t amount);

}  // namespace ui
