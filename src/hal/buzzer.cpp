#include "hal/buzzer.h"
#include <Arduino.h>
#include "config.h"
#include "data/settings.h"

static bool muted = false;

void buzzerInit() {
  pinMode(cfg::BUZZER_PIN, OUTPUT);
  digitalWrite(cfg::BUZZER_PIN, LOW);
  // tone() uses LED PWM channel 0. Setting it up once here avoids a harmless
  // "ledc_get_duty: LEDC is not initialized" error on the first tone (a core bug).
  ledcSetup(0, 2000, 10);
}

// tone() with a duration returns immediately; the core plays queued tones in order.
void buzzerClick() {
  if (!muted && settings().clickSounds) tone(cfg::BUZZER_PIN, 2000, 15);
}

void buzzerChime() {
  if (muted || !settings().chime) return;
  tone(cfg::BUZZER_PIN, 880, 120);
  tone(cfg::BUZZER_PIN, 1320, 180);
}

void buzzerAlert(bool up) {
  if (muted) return;   // alerts have their own on/off (Settings > Price alerts)
  static const unsigned int NOTES[] = {880, 1175, 1568};   // A5, D6, G6
  for (int i = 0; i < 3; i++) {
    tone(cfg::BUZZER_PIN, NOTES[up ? i : 2 - i], 110);
  }
}

// Deliberately unlike buzzerAlert's scale, so a target is recognisable by ear.
void buzzerTarget() {
  if (muted) return;   // targets have no separate switch: clear the target to stop them
  static const unsigned int NOTES[] = {1568, 2093, 1568, 2093};   // G6, C7, G6, C7
  for (unsigned int note : NOTES) tone(cfg::BUZZER_PIN, note, 90);
}

void buzzerSetMuted(bool m) {
  muted = m;
}

bool buzzerMuted() {
  return muted;
}
