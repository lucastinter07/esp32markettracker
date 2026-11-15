#include "ui/palette.h"
#include "data/settings.h"

namespace ui {

// 0xRRGGBB -> RGB565, at compile time (C++11 constexpr: one return statement each).
constexpr uint16_t rgb(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}
constexpr uint32_t channel(uint32_t c, int shift) { return (c >> shift) & 0xFF; }
constexpr uint32_t mixChannel(uint32_t fg, uint32_t bg, int shift, uint32_t a) {
  return ((channel(fg, shift) * a + channel(bg, shift) * (256 - a)) / 256) << shift;
}
// fg over bg at alpha a/256
constexpr uint16_t blend(uint32_t fg, uint32_t bg, uint32_t a) {
  return rgb(mixChannel(fg, bg, 16, a) | mixChannel(fg, bg, 8, a) | mixChannel(fg, bg, 0, a));
}

// The design canvas's themes ("Layout B · color themes").
static const Palette PALETTES[THEME_COUNT] = {
    {"Graphite", rgb(0x16181B), rgb(0x22252A), rgb(0x2B2E33), rgb(0x0F1012), rgb(0x2A2D31), rgb(0xECEDEE),
     rgb(0x8B9096), rgb(0x5C6168), rgb(0xECEDEE), rgb(0x16181B), blend(0xECEDEE, 0x16181B, 26),
     rgb(0x3DDC84), rgb(0xFF6B6B), blend(0x3DDC84, 0x16181B, 36), blend(0xFF6B6B, 0x16181B, 36),
     rgb(0xF5C451), blend(0xF5C451, 0x16181B, 36), rgb(0x0B0C0E), rgb(0x3A3E44), rgb(0x4A4F55),
     rgb(0x26292E), rgb(0x147846), rgb(0x962D2D)},
    {"Mint", rgb(0x16181B), rgb(0x22252A), rgb(0x2B2E33), rgb(0x0F1012), rgb(0x2A2D31), rgb(0xECEDEE),
     rgb(0x8B9096), rgb(0x5C6168), rgb(0x5EEAD4), rgb(0x0F1012), blend(0x5EEAD4, 0x16181B, 31),
     rgb(0x3DDC84), rgb(0xFF6B6B), blend(0x3DDC84, 0x16181B, 36), blend(0xFF6B6B, 0x16181B, 36),
     rgb(0xF5C451), blend(0xF5C451, 0x16181B, 36), rgb(0x0B0C0E), rgb(0x3A3E44), rgb(0x4A4F55),
     rgb(0x26292E), rgb(0x147846), rgb(0x962D2D)},
    {"Midnight", rgb(0x0B1220), rgb(0x1A2436), rgb(0x22314D), rgb(0x111A2B), rgb(0x26324A), rgb(0xF1F5F9),
     rgb(0x94A3B8), rgb(0x64748B), rgb(0x38BDF8), rgb(0x0B1220), blend(0x38BDF8, 0x0B1220, 31),
     rgb(0x22C55E), rgb(0xF05252), blend(0x22C55E, 0x0B1220, 38), blend(0xF05252, 0x0B1220, 38),
     rgb(0xFBBF24), blend(0xFBBF24, 0x0B1220, 36), rgb(0x070C16), rgb(0x334155), rgb(0x475569),
     rgb(0x243044), rgb(0x0A783C), rgb(0xA02832)},
    {"Amber", rgb(0x15130F), rgb(0x221E18), rgb(0x2C271F), rgb(0x0E0C09), rgb(0x322C23), rgb(0xF2EDE4),
     rgb(0xA39A8A), rgb(0x6E665A), rgb(0xFFB547), rgb(0x15130F), blend(0xFFB547, 0x15130F, 31),
     rgb(0x5BD68A), rgb(0xF2726B), blend(0x5BD68A, 0x15130F, 36), blend(0xF2726B, 0x15130F, 36),
     rgb(0x7DD3FC), blend(0x7DD3FC, 0x15130F, 36), rgb(0x0E0C09), rgb(0x3D362C), rgb(0x4D453A),
     rgb(0x28241E), rgb(0x287846), rgb(0x96372D)},
    {"Blue", rgb(0x000000), rgb(0x141518), rgb(0x1C1E23), rgb(0x000000), rgb(0x222429), rgb(0xF5F5F7),
     rgb(0x8E8E93), rgb(0x5A5A60), rgb(0x4C8DFF), rgb(0xFFFFFF), blend(0x4C8DFF, 0x000000, 36),
     rgb(0x30D158), rgb(0xFF453A), blend(0x30D158, 0x000000, 38), blend(0xFF453A, 0x000000, 38),
     rgb(0xFFD60A), blend(0xFFD60A, 0x000000, 36), rgb(0x000000), rgb(0x2C2C2E), rgb(0x3A3A3C),
     rgb(0x1C1C1E), rgb(0x14783C), rgb(0x96231E)},
};

const Palette& palette(int i) {
  return PALETTES[(i >= 0 && i < THEME_COUNT) ? i : 0];
}

const Palette& pal() {
  return palette(settings().theme);
}

}  // namespace ui
