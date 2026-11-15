#include "ui/palette.h"
#include "data/settings.h"

namespace ui {

// 0xRRGGBB -> RGB565, at compile time.
constexpr uint16_t rgb(uint32_t c) {
  return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

// Field order: name, bg, bar, ink, text, label, dim, faint, line, edge, panel, hi, stripe,
// accent, accentInk, up, down, target, heatNeutral, heatUp, heatDown.
static const Palette PALETTES[THEME_COUNT] = {
    // Graphite (default): grey bars, white text, amber targets
    {"GRAPHITE", rgb(0x16181B), rgb(0x2B2E33), rgb(0xECEDEE), rgb(0xECEDEE), rgb(0xECEDEE), rgb(0x8B9096),
     rgb(0x5C6168), rgb(0x2A2D31), rgb(0x3A3E44), rgb(0x22252A), rgb(0x2B2E33), rgb(0xECEDEE), rgb(0xECEDEE),
     rgb(0x16181B), rgb(0x3DDC84), rgb(0xFF6B6B), rgb(0xF5C451), rgb(0x26292E), rgb(0x147846), rgb(0x962D2D)},
    // Amber: black and amber, like a trading terminal; targets in sky blue
    {"AMBER", rgb(0x000000), rgb(0xFFB000), rgb(0x000000), rgb(0xE8E8E8), rgb(0xFFB000), rgb(0x7A7A7A),
     rgb(0x4A4A4A), rgb(0x2A2A2A), rgb(0x3A3A3A), rgb(0x141414), rgb(0x1F1904), rgb(0xFFB000), rgb(0xFFB000),
     rgb(0x000000), rgb(0x33D17A), rgb(0xFF5F57), rgb(0x7DD3FC), rgb(0x1E1E1E), rgb(0x00703C), rgb(0x962626)},
    // Classic: the pre-Phase-9 colors (grey bars, amber labels, blue selection)
    {"CLASSIC", rgb(0x000000), rgb(0x191C19), rgb(0xFFFFFF), rgb(0xFFFFFF), rgb(0xFFC200), rgb(0x848284),
     rgb(0x505450), rgb(0x303430), rgb(0x505450), rgb(0x191C19), rgb(0x293D5A), rgb(0xFFC200), rgb(0x293D5A),
     rgb(0xFFFFFF), rgb(0x00CA52), rgb(0xFF5152), rgb(0xFFC200), rgb(0x262A26), rgb(0x007A33), rgb(0xB3261E)},
    // Phosphor: an old green-screen monitor; red only for losses
    {"PHOSPHOR", rgb(0x020A04), rgb(0x33FF66), rgb(0x021006), rgb(0xB8FFC8), rgb(0x33FF66), rgb(0x2F7A44),
     rgb(0x1F5230), rgb(0x0E2A16), rgb(0x1A4A28), rgb(0x06160A), rgb(0x0B2A14), rgb(0x33FF66), rgb(0x33FF66),
     rgb(0x021006), rgb(0x7CFFA0), rgb(0xFF5F57), rgb(0xE8F060), rgb(0x0A1E0E), rgb(0x0E7A30), rgb(0x8A2420)},
    // Ice: near-black blue with a cyan accent
    {"ICE", rgb(0x05080C), rgb(0x00D1FF), rgb(0x031018), rgb(0xE6F4FA), rgb(0x00D1FF), rgb(0x5C7A8A),
     rgb(0x3A5260), rgb(0x12222C), rgb(0x1E3644), rgb(0x0B141C), rgb(0x0A2230), rgb(0x00D1FF), rgb(0x00D1FF),
     rgb(0x031018), rgb(0x19E68C), rgb(0xFF4D6D), rgb(0xFFC857), rgb(0x101A22), rgb(0x0A7850), rgb(0x8C2238)},
};

const Palette& palette(int i) {
  return PALETTES[(i >= 0 && i < THEME_COUNT) ? i : 0];
}

const Palette& pal() {
  return palette(settings().theme);
}

}  // namespace ui
