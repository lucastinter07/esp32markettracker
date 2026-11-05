// The four navigation buttons. Debounces them and posts an InputEvent on each press.
#pragma once
#include <stdint.h>

void buttonsInit();
void buttonsPoll(uint32_t nowMs);
bool buttonsBackHeld();   // true while BACK is held down (checked once at power-on)
