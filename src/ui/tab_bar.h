// The bottom tab bar on the main screens (Phase 9 design): Home · Sectors · Clock · Settings.
// The active tab is in the accent color with a 2 px line along its top edge.
#pragma once
#include <stdint.h>

namespace tabbar {

enum Tab : uint8_t { Home, Sectors, Clock, Settings, COUNT };

constexpr int16_t Y = 284;   // the bar covers y 284..319
constexpr int16_t H = 36;

void draw(Tab active);
int tabAt(int16_t x, int16_t y);   // the Tab a tap hit, or -1 if it's above the bar

}  // namespace tabbar
