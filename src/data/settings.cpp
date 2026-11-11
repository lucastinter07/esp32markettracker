#include "data/settings.h"
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// Saved as one block of bytes. Version 2 added the cycle options at the end of the struct;
// a version-1 block is shorter and simply fills the fields it had. A future change that
// isn't "add fields at the end" must bump the version and drop older blocks.
static const uint8_t SETTINGS_VERSION = 2;

static Settings current;
static Preferences prefs;

void settingsInit() {
  prefs.begin("settings", false);
  uint8_t version = prefs.getUChar("version", 0);
  size_t length = prefs.getBytesLength("data");
  Settings saved;   // starts with the defaults
  bool ok = (version == 1 || version == SETTINGS_VERSION) && length > 0 && length <= sizeof(saved) &&
            prefs.getBytes("data", &saved, length) == length;
  prefs.end();
  if (ok) current = saved;
  Serial.printf("settings: %s\n", ok ? (version == SETTINGS_VERSION ? "loaded" : "loaded (older format)") : "using defaults");
}

const Settings& settings() {
  return current;
}

Settings& settingsForEdit() {
  return current;
}

void settingsSave() {
  prefs.begin("settings", false);
  prefs.putUChar("version", SETTINGS_VERSION);
  prefs.putBytes("data", &current, sizeof(current));
  prefs.end();
}

// Home time, then time on each stock, per speed. NORMAL is config.h's timing.
static const uint32_t CYCLE_MS[3][2] = {
    {15000, 10000},
    {cfg::HOME_DURATION_MS, cfg::DETAIL_DURATION_MS},
    {60000, 30000},
};

static uint8_t speedIndex() {
  return current.cycleSpeed <= CYCLE_SLOW ? current.cycleSpeed : CYCLE_NORMAL;
}

uint32_t cycleHomeMs() {
  return CYCLE_MS[speedIndex()][0];
}

uint32_t cycleDetailMs() {
  return CYCLE_MS[speedIndex()][1];
}

const char* cycleSpeedName(uint8_t speed) {
  switch (speed) {
    case CYCLE_FAST: return "FAST";
    case CYCLE_SLOW: return "SLOW";
    default:         return "NORMAL";
  }
}
