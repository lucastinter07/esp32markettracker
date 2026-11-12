// Sector heat map maths: tile layout, text fitting and tile colors. Pure C++, tested on the PC.
#pragma once
#include <stdint.h>

struct TileRect {
  int16_t x, y, w, h;
};

// Squarified treemap: splits the w x h box at (x, y) into one tile per weight, each tile's
// area in proportion to its weight, keeping tiles as close to square as possible.
// - weights must be sorted largest first (the algorithm's assumption), count <= 16.
// - minWeight floors each weight first, so a tiny sector still gets a tile big enough
//   for its text (0 = no floor).
// - gap: pixels between neighbouring tiles. With gap 0 the tiles cover the box exactly.
void squarify(const float* weights, int count, float minWeight, int16_t x, int16_t y, int16_t w, int16_t h,
              int16_t gap, TileRect* out);

// How a tile's text is drawn with the built-in font (6 x 8 px per character at size 1).
struct TileText {
  uint8_t pad;         // inner margin, px
  uint8_t symSize;     // text size of the symbol (1 or 2)
  uint8_t pctSize;     // text size of the % change (1..3)
  bool pctDecimals;    // false: "-10%" instead of "-10.4%" (the one-decimal text didn't fit)
  bool showName;       // sector name under the symbol
  bool showPrice;      // price on the symbol's line, right-aligned
};

// Picks text sizes so nothing is clipped: the % change gets the largest size that fits the
// width (one decimal, or none if even size 1 is too wide); the symbol 2, else 1; then both
// shrink until they fit the height. Name and price only appear when they fit too.
// Lengths are in characters; pctLen1 / pctLen0 = the % text with one / no decimal.
TileText fitTile(int16_t w, int16_t h, int symLen, int nameLen, int priceLen, int pctLen1, int pctLen0);

// The tile's height in px that fitTile's choices need (for tests): pad, symbol, gap, % text.
int16_t tileTextHeight(const TileText& t, bool withName);

// Tile color for a % change: `neutral` at 0, moving toward `up` (or `down`) and reaching it
// at +/- saturatePct. A square-root curve, so small moves are still visible. RGB565.
uint16_t heatColor(float changePct, float saturatePct, uint16_t neutral, uint16_t up, uint16_t down);
