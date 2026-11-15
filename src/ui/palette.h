// Color themes (Phase 9, layout A). Every redesigned screen takes its colors from ui::pal(),
// so Settings > Theme changes the whole UI at once. Graphite is the default. All RGB565.
// Reference: design canvas, pages "★ FINAL · Layout A · Graphite" and "Layout A · pixel-exact
// themes".
#pragma once
#include <stdint.h>

namespace ui {

struct Palette {
  const char* name;     // shown in Settings, e.g. "GRAPHITE"
  uint16_t bg;          // screen background
  uint16_t bar;         // header bars
  uint16_t ink;         // text on the header bars
  uint16_t text;        // prices and main text
  uint16_t label;       // symbols, Settings values, hints that can be tapped
  uint16_t dim;         // secondary text: column headers, axis labels, units
  uint16_t faint;       // least important text (hints)
  uint16_t line;        // row separators, chart grid
  uint16_t edge;        // outlines of buttons and cards, empty progress steps
  uint16_t panel;       // cards and tiles
  uint16_t hi;          // the highlighted row
  uint16_t stripe;      // the highlighted row's left edge
  uint16_t accent;      // selected buttons
  uint16_t accentInk;   // text on accent
  uint16_t up, down;
  uint16_t target;      // price targets, earnings, notices such as DEMO PRICES
  uint16_t heatNeutral, heatUp, heatDown;   // sector map tiles (0 %, full green, full red)
};

constexpr int THEME_COUNT = 5;   // Graphite, Amber, Classic, Phosphor, Ice

const Palette& pal();             // the current theme (Settings > Theme)
const Palette& palette(int i);    // any theme by number

}  // namespace ui
