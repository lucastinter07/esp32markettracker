// Owns the screen and touch hardware. The rest of the code draws through display()
// and receives touches as InputEvents, so the panel/driver details stay in this file.
#pragma once
#include <stdint.h>
#include "DFRobot_GDL.h"

void displayInit();
DFRobot_GDL& display();

// The panel's pixel format: 3 bytes per pixel (RGB666) or 2 (RGB565, cfg::DISPLAY_16BIT).
uint8_t displayBytesPerPixel();
// An RGB565 color in the panel's format (displayBytesPerPixel() bytes). Fills use the same
// conversion, so a text box and a filled area of the same color match to the bit.
void displayPixelBytes(uint16_t color, uint8_t* out);

// Writes a w x h block of pixels in one transfer: displayBytesPerPixel() bytes per pixel
// (from displayPixelBytes), row by row.
void displayPushPixels(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* pixels);

// Checks the touch panel and posts a Tap event when a finger first lands
// (holding a finger down does not repeat the tap).
void touchPoll(uint32_t nowMs);

// Debug aid (cfg::TOUCH_DEBUG): a small magenta cross where a tap registered.
void drawTouchMarker(int16_t x, int16_t y);
