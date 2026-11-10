// User settings, changed on the Settings screen and saved in NVS (flash), so they survive
// power cuts and firmware uploads.
#pragma once
#include <stdint.h>

struct Settings {
  bool clickSounds = true;     // click on every button press / tap
  bool chime = true;           // chime at market open and close (6H), and at boot
  uint8_t alertPercent = 0;    // price alert threshold, e.g. 3 = +/-3% on the day; 0 = off (6G)
  bool nightDim = true;        // dim the screen at night (6H)
  uint8_t nightStart = 22;     // hour the dimming starts (24 h clock, ET)
  uint8_t nightEnd = 7;        // hour it ends
  uint8_t brightness = 100;    // normal screen brightness, percent
};

void settingsInit();               // loads saved settings (or the defaults above)
const Settings& settings();        // read the current settings
Settings& settingsForEdit();       // change them, then call settingsSave()
void settingsSave();
