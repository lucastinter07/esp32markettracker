// Night dimming rules: which hours count as night, and how bright the screen should be.
// Pure C++, unit-tested on the PC.
#pragma once
#include <stdint.h>

static const uint8_t NIGHT_BRIGHTNESS = 50;   // percent, during night hours

// True if `hour` (0-23) is inside [start, end). Wraps past midnight: 22 -> 7 covers
// 22:00-06:59. start == end means no night hours.
bool isNightHour(int hour, int start, int end);

// The backlight percent to use: the user's brightness, capped at NIGHT_BRIGHTNESS during
// night hours, unless dimming is off or the screen was touched recently (awake).
uint8_t targetBrightness(uint8_t userPercent, bool nightDim, bool night, bool awake);
