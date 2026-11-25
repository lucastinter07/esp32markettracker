// Device health: why the chip last started, memory, and an hourly log line for long test
// runs. Shown on the Diagnostics screen (Settings > Diagnostics).
#pragma once
#include <stdint.h>

struct MemoryInfo {
  uint32_t freeBytes;      // free heap right now
  uint32_t lowestBytes;    // the least free heap since boot
  uint32_t largestBlock;   // biggest single free block (a new HTTPS connection needs ~40 KB)
  uint32_t psramFree;      // external RAM, 0 if the board has none enabled
};

const char* healthResetReason();    // "power on", "CRASH (panic)", ...
void healthNoteWatchdogRestart();   // before ESP.restart() by the quote watchdog: the next boot says so
void healthLogBoard();              // boot: flash chip size (8 or 16 MB: N8R8 vs N16R8) and PSRAM
void healthMemory(MemoryInfo& out);
void healthLogHourly(uint32_t nowMs);   // call every loop; prints at 2 min, then once an hour
