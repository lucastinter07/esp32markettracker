// Startup screen (Phase 9 design): logo and title, three boot steps that check off
// (Wi-Fi, Clock, Prices), a ring counting the prices in, and a faint chart behind it all.
#pragma once
#include <stdint.h>

namespace splash {

enum class Step : uint8_t { Waiting, Done, Problem };

struct Boot {
  Step wifi;
  const char* wifiDetail;    // "connected", "connecting" ..., "setup needed"
  Step clock;
  char clockDetail[20];      // "synced 9:41 AM", "waiting"
  int priced, total;         // symbols with a price / all symbols
};

void drawFull();
void update(const Boot& boot);   // redraws only the rows / ring whose content changed

}  // namespace splash
