// Time-of-day behaviour (6H): the open/close chime at the start and end of each trading
// session, and night dimming (Settings > Night dimming / Night hours), where any input
// brings back full brightness for WAKE_MS.
#pragma once
#include <stdint.h>

void scheduleUpdate(uint32_t nowMs);   // call every loop
void scheduleWake(uint32_t nowMs);     // call on any button or touch input
