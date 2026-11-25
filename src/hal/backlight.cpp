#include "hal/backlight.h"
#include <Arduino.h>
#include "config.h"

// LED PWM channel 2. tone() (the buzzer) uses channel 0, and channels 0 and 1 share a
// timer, so the backlight uses channel 2 (a different timer) to avoid the buzzer changing
// the backlight's frequency.
static const uint8_t CHANNEL = 2;
static const uint32_t FREQ_HZ = 5000;
static const uint8_t RESOLUTION_BITS = 8;   // duty 0-255

void backlightInit() {
  // The display library just switched the pin fully on; attaching it to PWM takes over.
  ledcSetup(CHANNEL, FREQ_HZ, RESOLUTION_BITS);
  ledcAttachPin(cfg::TFT_BL, CHANNEL);
  backlightSet(100);
}

static uint8_t level = 100;   // the level set with backlightSet(); fades return to it

static void writeLevel(uint8_t percent) {
  // Eyes judge brightness on a curve, not linearly: half the power looks much brighter
  // than half. Squaring the fraction makes "50%" look about half as bright.
  uint32_t duty = (uint32_t)percent * percent * 255 / 10000;
  if (percent > 0 && duty == 0) duty = 1;
  ledcWrite(CHANNEL, duty);
}

void backlightSet(uint8_t percent) {
  if (percent > 100) percent = 100;
  level = percent;
  writeLevel(percent);
}

// Steps in percent, which the squared curve in writeLevel() turns into an even-looking fade.
static void fade(uint8_t from, uint8_t to, uint32_t ms) {
  const int STEPS = 12;
  for (int i = 1; i <= STEPS; i++) {
    writeLevel((uint8_t)(from + ((int)to - from) * i / STEPS));
    delay(ms / STEPS);
  }
}

void backlightFadeOut(uint32_t ms) {
  fade(level, 0, ms);
}

void backlightFadeIn(uint32_t ms) {
  fade(0, level, ms);
}
