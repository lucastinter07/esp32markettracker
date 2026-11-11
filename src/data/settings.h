// User settings, changed on the Settings screen and saved in NVS (flash), so they survive
// power cuts and firmware uploads.
#pragma once
#include <stdint.h>

enum CycleSpeed : uint8_t { CYCLE_FAST = 0, CYCLE_NORMAL = 1, CYCLE_SLOW = 2 };

// New fields go at the END, so a block saved by older firmware still lines up with the
// fields it had (the new ones keep their defaults).
struct Settings {
  bool clickSounds = true;     // click on every button press / tap
  bool chime = true;           // chime at market open and close (6H), and at boot
  uint8_t alertPercent = 0;    // price alert threshold, e.g. 3 = +/-3% on the day; 0 = off (6G)
  bool nightDim = true;        // dim the screen at night (6H)
  uint8_t nightStart = 22;     // hour the dimming starts (24 h clock, ET)
  uint8_t nightEnd = 7;        // hour it ends
  uint8_t brightness = 100;    // normal screen brightness, percent
  bool autoCycle = true;       // Home -> each stock -> Home ...; off = stay on Home (Phase 7)
  uint8_t cycleSpeed = CYCLE_NORMAL;
};

void settingsInit();               // loads saved settings (or the defaults above)
const Settings& settings();        // read the current settings
Settings& settingsForEdit();       // change them, then call settingsSave()
void settingsSave();

// How long the auto cycle stays on Home, and on each stock, at the chosen speed.
uint32_t cycleHomeMs();
uint32_t cycleDetailMs();
const char* cycleSpeedName(uint8_t speed);   // "FAST", "NORMAL", "SLOW"
