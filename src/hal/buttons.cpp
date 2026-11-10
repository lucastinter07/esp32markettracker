#include "hal/buttons.h"
#include <Arduino.h>
#include "config.h"
#include "app/input_event.h"

struct Button {
  uint8_t pin;
  InputType event;
  bool hasLongPress;     // posts LongBack when held (BACK only)
  bool stablePressed;    // debounced state
  bool lastRaw;          // last raw reading
  uint32_t lastChangeMs; // when the raw reading last changed
  uint32_t pressedAtMs;  // when the current press started
  bool longSent;         // the long-press event for this press has been posted
};

static Button buttons[] = {
    {cfg::BTN_PREV, InputType::Prev, false, false, false, 0, 0, false},
    {cfg::BTN_NEXT, InputType::Next, false, false, false, 0, 0, false},
    {cfg::BTN_SELECT, InputType::Select, false, false, false, 0, 0, false},
    {cfg::BTN_BACK, InputType::Back, true, false, false, 0, 0, false},
};

void buttonsInit() {
  for (Button& b : buttons) {
    pinMode(b.pin, INPUT_PULLUP);   // unpressed = HIGH, pressed = LOW
  }
  delay(5);   // let the pull-ups settle before the first reading
  // Start from the current state, so a button already held at power-on
  // (e.g. BACK to enter Wi-Fi setup) doesn't also count as a press.
  for (Button& b : buttons) {
    b.stablePressed = b.lastRaw = digitalRead(b.pin) == LOW;
    b.longSent = b.stablePressed;   // and not as a long press either (it'd open Settings)
  }
}

bool buttonsBackHeld() {
  return digitalRead(cfg::BTN_BACK) == LOW;
}

void buttonsPoll(uint32_t nowMs) {
  for (Button& b : buttons) {
    bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.lastRaw) {
      b.lastRaw = raw;
      b.lastChangeMs = nowMs;
    }
    // Only accept a new state once the reading has stopped bouncing.
    if (raw != b.stablePressed && nowMs - b.lastChangeMs >= cfg::DEBOUNCE_MS) {
      b.stablePressed = raw;
      if (raw) {
        inputPost(InputEvent{b.event, 0, 0});   // the normal press, straight away
        b.pressedAtMs = nowMs;
        b.longSent = false;
      }
    }
    // Still held after LONG_PRESS_MS: one extra "long press" event for this press.
    if (b.hasLongPress && b.stablePressed && !b.longSent && nowMs - b.pressedAtMs >= cfg::LONG_PRESS_MS) {
      inputPost(InputEvent{InputType::LongBack, 0, 0});
      b.longSent = true;
    }
  }
}
