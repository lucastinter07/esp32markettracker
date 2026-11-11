#include "app/schedule.h"
#include <Arduino.h>
#include "data/settings.h"
#include "hal/backlight.h"
#include "hal/buzzer.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "night_mode.h"

static const uint32_t WAKE_MS = 30000;   // full brightness after an input at night

static int8_t lastOpen = -1;   // -1 = unknown (just booted / clock not synced yet)
static uint32_t wokeAtMs = 0;
static bool everWoken = false;
static int appliedBrightness = -1;

void scheduleWake(uint32_t nowMs) {
  wokeAtMs = nowMs;
  everWoken = true;
}

void scheduleUpdate(uint32_t nowMs) {
  EtTime now;
  bool haveTime = timeNowET(now);

  // Chime on real transitions only: booting during the session isn't an "open".
  // marketStatus() knows weekends, holidays and 13:00 early closes.
  if (haveTime) {
    bool open = marketStatus(now).phase == MarketPhase::Open;
    if (lastOpen >= 0 && open != (lastOpen == 1)) {
      Serial.printf("schedule: market %s, chime\n", open ? "open" : "closed");
      buzzerChime();   // respects Settings > Open/close chime
    }
    lastOpen = open ? 1 : 0;
  }

  const Settings& st = settings();
  bool night = haveTime && isNightHour(now.hour, st.nightStart, st.nightEnd);
  bool awake = everWoken && nowMs - wokeAtMs < WAKE_MS;
  int target = targetBrightness(st.brightness, st.nightDim, night, awake);
  if (target != appliedBrightness) {
    if (appliedBrightness >= 0) Serial.printf("schedule: brightness %d%%\n", target);
    backlightSet((uint8_t)target);
    appliedBrightness = target;
  }
}
