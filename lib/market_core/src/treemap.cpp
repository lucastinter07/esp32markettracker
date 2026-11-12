#include "treemap.h"
#include <math.h>

static const int MAX_TILES = 16;

// The worst aspect ratio in a row of tiles laid along a side of length `side`
// (1 = all square). Adding a tile to the row is only worth it if this doesn't get worse.
static float worstRatio(const float* areas, int count, float side) {
  float sum = 0, largest = 0, smallest = 1e30f;
  for (int i = 0; i < count; i++) {
    sum += areas[i];
    if (areas[i] > largest) largest = areas[i];
    if (areas[i] < smallest) smallest = areas[i];
  }
  float a = side * side * largest / (sum * sum);
  float b = (sum * sum) / (side * side * smallest);
  return a > b ? a : b;
}

// Pixel edges are rounded the same way for both neighbours, so tiles meet exactly.
static TileRect toPixels(float x, float y, float w, float h, int16_t gap) {
  float half = gap / 2.0f;
  int16_t x0 = (int16_t)lroundf(x + half), y0 = (int16_t)lroundf(y + half);
  int16_t x1 = (int16_t)lroundf(x + w - half), y1 = (int16_t)lroundf(y + h - half);
  TileRect r;
  r.x = x0;
  r.y = y0;
  r.w = x1 - x0;
  r.h = y1 - y0;
  return r;
}

void squarify(const float* weights, int count, float minWeight, int16_t x, int16_t y, int16_t w, int16_t h,
              int16_t gap, TileRect* out) {
  if (count <= 0) return;
  if (count > MAX_TILES) count = MAX_TILES;
  float floored[MAX_TILES];
  float total = 0;
  for (int i = 0; i < count; i++) {
    floored[i] = weights[i] > minWeight ? weights[i] : minWeight;
    total += floored[i];
  }
  // Areas in px^2, and the part of the box still free.
  float areas[MAX_TILES];
  for (int i = 0; i < count; i++) areas[i] = floored[i] * (float)w * (float)h / total;
  float fx = x, fy = y, fw = w, fh = h;

  int rowStart = 0, rowEnd = 0;   // the row being built: areas[rowStart .. rowEnd)
  int done = 0;
  while (done < count) {
    float side = fw < fh ? fw : fh;
    bool better = rowEnd == rowStart || (rowEnd < count && worstRatio(areas + rowStart, rowEnd - rowStart + 1, side) <=
                                                               worstRatio(areas + rowStart, rowEnd - rowStart, side));
    if (rowEnd < count && better) {
      rowEnd++;
      continue;
    }
    // Lay the row along the shorter side of the free box, then shrink the box.
    float sum = 0;
    for (int i = rowStart; i < rowEnd; i++) sum += areas[i];
    if (fw >= fh) {   // a column on the left
      float colW = sum / fh, ty = fy;
      for (int i = rowStart; i < rowEnd; i++) {
        float th = areas[i] / colW;
        out[i] = toPixels(fx, ty, colW, th, gap);
        ty += th;
      }
      fx += colW;
      fw -= colW;
    } else {          // a row along the top
      float rowH = sum / fw, tx = fx;
      for (int i = rowStart; i < rowEnd; i++) {
        float tw = areas[i] / rowH;
        out[i] = toPixels(tx, fy, tw, rowH, gap);
        tx += tw;
      }
      fy += rowH;
      fh -= rowH;
    }
    done = rowEnd;
    rowStart = rowEnd;
  }
}

TileText fitTile(int16_t w, int16_t h, int symLen, int nameLen, int priceLen, int pctLen1, int pctLen0) {
  TileText t;
  t.pad = (w < 60 || h < 48) ? 3 : 6;
  int aw = w - 2 * t.pad, ah = h - 2 * t.pad;

  t.pctDecimals = true;
  t.pctSize = 0;
  for (int s = 3; s >= 1; s--) {
    if (pctLen1 * 6 * s <= aw) {
      t.pctSize = (uint8_t)s;
      break;
    }
  }
  if (t.pctSize == 0) {
    t.pctDecimals = false;
    t.pctSize = 1;   // the layout's minimum tile size guarantees this fits
    (void)pctLen0;
  }
  t.symSize = symLen * 12 <= aw ? 2 : 1;
  while (t.symSize * 8 + 4 + t.pctSize * 8 > ah && t.pctSize > 1) t.pctSize--;
  while (t.symSize * 8 + 4 + t.pctSize * 8 > ah && t.symSize > 1) t.symSize--;

  t.showName = nameLen * 6 <= aw && t.symSize * 8 + 5 + 8 + 4 + t.pctSize * 8 <= ah;
  t.showPrice = symLen * 6 * t.symSize + 6 + priceLen * 6 <= aw;
  return t;
}

int16_t tileTextHeight(const TileText& t, bool withName) {
  return (int16_t)(2 * t.pad + t.symSize * 8 + (withName ? 5 + 8 : 0) + 4 + t.pctSize * 8);
}

static uint16_t mix(uint16_t from, uint16_t to, float f) {
  int r0 = (from >> 11) & 0x1F, g0 = (from >> 5) & 0x3F, b0 = from & 0x1F;
  int r1 = (to >> 11) & 0x1F, g1 = (to >> 5) & 0x3F, b1 = to & 0x1F;
  int r = (int)lroundf(r0 + (r1 - r0) * f);
  int g = (int)lroundf(g0 + (g1 - g0) * f);
  int b = (int)lroundf(b0 + (b1 - b0) * f);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t heatColor(float changePct, float saturatePct, uint16_t neutral, uint16_t up, uint16_t down) {
  float f = fabsf(changePct) / saturatePct;
  if (f > 1) f = 1;
  f = sqrtf(f);
  return mix(neutral, changePct >= 0 ? up : down, f);
}
