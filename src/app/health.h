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
void healthMemory(MemoryInfo& out);
void healthLogHourly(uint32_t nowMs);   // call every loop; prints once an hour
