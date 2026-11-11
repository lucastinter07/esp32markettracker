#include "night_mode.h"

bool isNightHour(int hour, int start, int end) {
  if (start == end) return false;
  if (start < end) return hour >= start && hour < end;   // e.g. 0 -> 6
  return hour >= start || hour < end;                    // wraps midnight, e.g. 22 -> 7
}

uint8_t targetBrightness(uint8_t userPercent, bool nightDim, bool night, bool awake) {
  if (!nightDim || !night || awake) return userPercent;
  return userPercent < NIGHT_BRIGHTNESS ? userPercent : NIGHT_BRIGHTNESS;
}
