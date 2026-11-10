// One event type shared by buttons and touch.
// Buttons and touch post events, and the screen manager reads them. The screen
// logic never needs to know which input produced an event.
#pragma once
#include <stdint.h>

enum class InputType : uint8_t {
  Prev,
  Next,
  Select,
  Back,
  LongBack,   // BACK held for 2 s: opens Settings
  Tap,        // touch: x/y hold the screen position, and the active screen decides what was hit
};

struct InputEvent {
  InputType type;
  int16_t x;
  int16_t y;
};

void inputInit();
bool inputPost(const InputEvent& ev);   // safe to call from any task
bool inputGet(InputEvent& ev);          // non-blocking; false if nothing waiting
