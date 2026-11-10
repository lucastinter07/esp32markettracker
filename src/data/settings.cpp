#include "data/settings.h"
#include <Arduino.h>
#include <Preferences.h>

// Saved as one block of bytes. The version number changes whenever the Settings struct
// changes, so an older saved block is ignored instead of being read into the wrong fields.
static const uint8_t SETTINGS_VERSION = 1;

static Settings current;
static Preferences prefs;

void settingsInit() {
  prefs.begin("settings", false);
  Settings saved;
  bool ok = prefs.getUChar("version", 0) == SETTINGS_VERSION &&
            prefs.getBytes("data", &saved, sizeof(saved)) == sizeof(saved);
  prefs.end();
  if (ok) current = saved;
  Serial.println(ok ? "settings: loaded" : "settings: using defaults");
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
