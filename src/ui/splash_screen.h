// Startup screen (Phase 9, layout A, design "S2 + logo"): candlestick logo and title at the
// top left, a rising chart across the screen, a boot log (Wi-Fi, clock, prices) at the
// bottom left, and the share of prices loaded as a big percentage and a step bar.
#pragma once
#include <stdint.h>

namespace splash {

enum class Step : uint8_t { Waiting, Done, Problem };

struct Boot {
  Step wifi;     // Problem = Wi-Fi setup needed
  Step clock;
  int priced;    // symbols with a price
  int total;     // all symbols (one bar step each)
};

void drawFull();
void update(const Boot& boot);   // redraws only the lines / percentage that changed

}  // namespace splash
