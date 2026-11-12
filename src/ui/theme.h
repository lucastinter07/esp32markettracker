// Colors (RGB565) and layout constants shared by every screen.
#pragma once
#include <stdint.h>

namespace theme {

constexpr uint16_t BG        = 0x0000;  // black
constexpr uint16_t PANEL     = 0x18E3;  // dark gray
constexpr uint16_t HIGHLIGHT = 0x29EB;  // muted blue, selected row
constexpr uint16_t TEXT      = 0xFFFF;  // white
constexpr uint16_t DIM       = 0x8410;  // mid gray: faded values (stale prices, "waiting...")
constexpr uint16_t LABEL     = 0xC638;  // light gray: labels and secondary text, readable on black
constexpr uint16_t UP        = 0x064A;  // green
constexpr uint16_t DOWN      = 0xFA8A;  // red
constexpr uint16_t ACCENT    = 0xFE00;  // amber
constexpr uint16_t STAT_LABEL = ACCENT;  // labels on the detail screen's stats (OPEN, 52W HIGH...)
constexpr uint16_t GRID      = 0x31A6;  // #303430: faint chart gridlines
constexpr uint16_t FRAME     = 0x52AA;  // #505450: chart border

// Sector heat map tiles: neutral grey at 0 %, full green / red at +/- cfg::SECTOR_SATURATE_PCT.
// Dark enough that white text stays readable on every shade.
constexpr uint16_t HEAT_NEUTRAL = 0x2144;  // #262A26
constexpr uint16_t HEAT_UP      = 0x03C6;  // #007A33
constexpr uint16_t HEAT_DOWN    = 0xB123;  // #B3261E

inline uint16_t changeColor(float change) {
  return change >= 0 ? UP : DOWN;
}

// Home screen layout (480 x 320)
constexpr int16_t HEADER_H = 30;
constexpr int16_t ROW_Y0   = 32;
constexpr int16_t ROW_H    = 44;
constexpr int16_t FOOTER_Y = ROW_Y0 + 5 * ROW_H + 4;   // 256

// The built-in font is 6x8 px per character at text size 1 (12x16 at size 2, etc.)
constexpr int16_t CHAR_W = 6;
constexpr int16_t CHAR_H = 8;

}  // namespace theme
