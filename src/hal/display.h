// Owns the screen and touch hardware. The rest of the code draws through display()
// and receives touches as InputEvents, so the panel/driver details stay in this file.
#pragma once
#include <stdint.h>
#include "DFRobot_GDL.h"

void displayInit();
DFRobot_GDL& display();

// Checks the touch panel and posts a Tap event when a finger first lands
// (holding a finger down does not repeat the tap).
void touchPoll(uint32_t nowMs);

// Debug aid (cfg::TOUCH_DEBUG): a small magenta cross where a tap registered.
void drawTouchMarker(int16_t x, int16_t y);
