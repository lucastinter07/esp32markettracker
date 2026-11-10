// Settings > Watchlist: the 5 stock slots, the 2 index slots and "Reset to defaults".
// Tapping a stock slot opens an on-screen keyboard; the typed ticker is checked with
// Finnhub (price + company name) and only used after the user confirms it.
#pragma once
#include <stdint.h>
#include "app/input_event.h"

namespace watchui {

enum class Result : uint8_t { Stay, Exit };   // Exit = back to the Settings screen

void open();                                  // shows the slot list
void update(uint32_t nowMs);                  // call every loop while showing
Result handle(const InputEvent& ev, uint32_t nowMs);
bool busy();                                  // waiting for a ticker check (don't time out)

}  // namespace watchui
