// The built-in 6 x 8 font's glyph data (see font6x8.cpp). Drawn by ui::text() in ui/gfx.h.
#pragma once
#include <stdint.h>

namespace ui {

extern const uint8_t FONT6X8[256 * 5];   // glyph c = bytes c*5 .. c*5+4, one per column

}  // namespace ui
