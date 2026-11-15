// Owns the screen and touch hardware. The rest of the code draws through display()
// and receives touches as InputEvents, so the panel/driver details stay in this file.
#pragma once
#include <stdint.h>
#include "DFRobot_GDL.h"

void displayInit();
DFRobot_GDL& display();

// Writes a w x h block of pixels in one transfer. `rgb666` holds 3 bytes per pixel (the
// panel's own color format: red, green, blue, each in the top 6 bits), row by row.
void displayPushPixels(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* rgb666);

// Checks the touch panel and posts a Tap event when a finger first lands
// (holding a finger down does not repeat the tap).
void touchPoll(uint32_t nowMs);

// Debug aid (cfg::TOUCH_DEBUG): a small magenta cross where a tap registered.
void drawTouchMarker(int16_t x, int16_t y);
