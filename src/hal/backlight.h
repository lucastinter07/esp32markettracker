// Screen brightness via PWM on the backlight pin: the pin is switched on and off 5000
// times a second, and the fraction of "on" time sets how bright the backlight looks.
#pragma once
#include <stdint.h>

void backlightInit();                 // call after displayInit()
void backlightSet(uint8_t percent);   // 0-100
// Screen-change fades (Settings > Transitions): down to dark, and back up to the level
// last set with backlightSet(). Each takes about `ms` and blocks while it runs.
void backlightFadeOut(uint32_t ms);
void backlightFadeIn(uint32_t ms);
