#include "app/health.h"
#include <Arduino.h>
#include <esp_system.h>

// Why the chip last started. Anything other than power-on / reset button / upload points
// at a crash (panic, watchdog) or a power dip (brownout): worth knowing after a long run.
const char* healthResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_SW:       return "software restart";
    case ESP_RST_PANIC:    return "CRASH (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "CRASH (watchdog)";
    case ESP_RST_BROWNOUT: return "POWER DIP (brownout)";
    default:               return "other (e.g. upload)";
  }
}

void healthMemory(MemoryInfo& out) {
  out.freeBytes = ESP.getFreeHeap();
  out.lowestBytes = ESP.getMinFreeHeap();
  out.largestBlock = ESP.getMaxAllocHeap();
  out.psramFree = ESP.getFreePsram();
}

// Once an hour: uptime and memory, for long test runs. "lowest" is the least free memory
// seen since boot; if "free" keeps falling hour after hour, something is leaking.
void healthLogHourly(uint32_t nowMs) {
  static uint32_t lastMs = 0;
  if (nowMs - lastMs < 3600000UL) return;
  lastMs = nowMs;
  MemoryInfo m;
  healthMemory(m);
  Serial.printf("health: up %luh, free memory %u bytes (lowest %u, largest block %u)\n",
                (unsigned long)(nowMs / 3600000UL), (unsigned)m.freeBytes, (unsigned)m.lowestBytes,
                (unsigned)m.largestBlock);
}
