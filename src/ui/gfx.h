// Drawing helpers for the redesigned screens (Phase 9): text in the UI fonts, rounded
// cards and pills, small icons drawn from shapes.
//
// Text: y is the BASELINE (where letters sit), like CSS's alphabetic baseline. textMid()
// centers text in a row instead. Custom fonts have no background color, so a screen paints
// the area behind text first (a card, or the background) and then draws the text on it.
// Glyphs are drawn as horizontal runs (one fillRect per run), which is much faster on the
// SPI display than the library's pixel-by-pixel print().
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "DFRobot_GDL.h"

namespace ui {

enum class Align : uint8_t { Left, Center, Right };

int16_t textWidth(const GFXfont& f, const char* s);
int16_t capHeight(const GFXfont& f);   // height of "H" above the baseline ("0" in digit-only fonts)

// Draws s with its baseline at y; x is the left edge, center or right edge per `a`.
// Returns the x just after the text.
int16_t text(int16_t x, int16_t y, const GFXfont& f, uint16_t color, const char* s, Align a = Align::Left);

// Same, vertically centered (by cap height) in the row y .. y+h.
int16_t textMid(int16_t x, int16_t y, int16_t h, const GFXfont& f, uint16_t color, const char* s,
                Align a = Align::Left);

// Copies s into out, shortened with an ellipsis if needed so it's at most maxW pixels wide.
void fitText(const GFXfont& f, const char* s, int16_t maxW, char* out, size_t size);

// Colors: `from` blended toward `to` by amount/255 (RGB565).
uint16_t mix(uint16_t from, uint16_t to, uint8_t amount);

// Shapes
void fillRound(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
void pill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);   // round ends
void dashedHLine(int16_t x, int16_t y, int16_t w, int16_t dash, int16_t gap, uint16_t color);
void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);   // 2 px
void checkMark(int16_t cx, int16_t cy, uint16_t color);                  // ~10 px wide
void chevron(int16_t cx, int16_t cy, bool left, uint16_t color);         // ~6 x 10 px
void triangle(int16_t cx, int16_t cy, int16_t size, bool up, uint16_t color);

}  // namespace ui
