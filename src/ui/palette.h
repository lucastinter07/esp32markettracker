// Color themes (Phase 9). Every redesigned screen takes its colors from ui::pal(), so
// Settings › Display › Theme changes the whole UI at once. Graphite is the default.
// All values are RGB565. The "soft" colors are the accent / up / down / target colors
// blended into the background, for pills and chart fills.
#pragma once
#include <stdint.h>

namespace ui {

struct Palette {
  const char* name;
  uint16_t bg;          // screen background
  uint16_t card;        // cards, chips, keys
  uint16_t cardHi;      // the selected card
  uint16_t nav;         // bottom tab bar, inner panels
  uint16_t border;      // separators
  uint16_t text;        // primary text
  uint16_t sub;         // secondary text
  uint16_t faint;       // tertiary text, axis labels
  uint16_t accent;      // selected tab, buttons, highlights
  uint16_t accentInk;   // text on the accent color
  uint16_t accentSoft;
  uint16_t up, down;
  uint16_t upSoft, downSoft;
  uint16_t target;      // price targets, earnings
  uint16_t targetSoft;
  uint16_t clockBg;     // the overnight clock's (darker) background
  uint16_t toggleOff;
  uint16_t grid;        // dashed reference lines on charts
  uint16_t heatNeutral, heatUp, heatDown;   // sector map tiles
};

constexpr int THEME_COUNT = 5;   // Graphite, Mint, Midnight, Amber, Blue

const Palette& pal();             // the current theme (Settings › Display › Theme)
const Palette& palette(int i);    // any theme, e.g. for the picker's swatches

}  // namespace ui
