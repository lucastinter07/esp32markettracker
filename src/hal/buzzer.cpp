#include "hal/buzzer.h"
#include <Arduino.h>
#include "config.h"

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
  if (!muted) tone(cfg::BUZZER_PIN, 2000, 15);
}

void buzzerChime() {
  if (muted) return;
  tone(cfg::BUZZER_PIN, 880, 120);
  tone(cfg::BUZZER_PIN, 1320, 180);
}

void buzzerSetMuted(bool m) {
  muted = m;
}

bool buzzerMuted() {
  return muted;
}
