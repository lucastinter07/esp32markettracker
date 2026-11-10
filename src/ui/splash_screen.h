// Startup screen shown while the device connects and downloads its first prices:
// a logo drawn from shapes, the creators' names, and a progress line + bar.
#pragma once
#include <stdint.h>

namespace splash {

void drawFull();
// Redraws the progress line and bar when they change. percent is 0-100.
void update(const char* status, int percent);

}  // namespace splash
