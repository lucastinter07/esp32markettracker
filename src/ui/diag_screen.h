// Settings > Diagnostics: live numbers about the device, the network and the data.
// Three tabs: DEVICE (firmware, uptime, last restart, memory, flash), NETWORK (Wi-Fi,
// clock, Finnhub and Twelve Data) and DATA (what each symbol has loaded, and how old it is).
#pragma once
#include <stdint.h>
#include "app/input_event.h"

namespace diagui {

enum class Result : uint8_t { Stay, Exit };   // Exit = back to the Settings screen

void open(uint32_t nowMs);
void update(uint32_t nowMs);   // call every loop; refreshes the values about once a second
Result handle(const InputEvent& ev, uint32_t nowMs);

}  // namespace diagui
