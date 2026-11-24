#include "app/health.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include "config.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"

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

// For long test runs (the soak): a baseline 2 minutes after boot, then once an hour.
//   health: up 5h02m, ET 14:02 open, free memory ... (lowest ..., largest block ...), psram free ...
//   health: wifi connected -58 dBm, 0 drops; finnhub 412 requests since last line (5321 total),
//           twelve data 14 today; 0 errors since last line (3 total); last quote 9 s ago; last error ...
// "lowest" is the least free memory seen since boot; if "free" keeps falling hour after hour,
// something is leaking. "largest block" must stay above ~40 KB (one HTTPS connection).
void healthLogHourly(uint32_t nowMs) {
  static uint32_t lastMs = 0;
  static bool baselineDone = false;
  static uint32_t lastFinnhub = 0, lastErrors = 0;
  if (!baselineDone) {
    if (nowMs < 120000UL) return;
    baselineDone = true;
  } else if (nowMs - lastMs < 3600000UL) {
    return;
  }
  lastMs = nowMs;

  uint32_t minutes = nowMs / 60000UL;
  char clock[24] = "ET --:--";
  EtTime et;
  if (timeNowET(et)) {
    snprintf(clock, sizeof(clock), "ET %02d:%02d %s", et.hour, et.minute, marketIsOpenNow() ? "open" : "closed");
  }
  MemoryInfo m;
  healthMemory(m);
  Serial.printf("health: up %luh%02lum, %s, free memory %u bytes (lowest %u, largest block %u), psram free %u\n",
                (unsigned long)(minutes / 60), (unsigned long)(minutes % 60), clock, (unsigned)m.freeBytes,
                (unsigned)m.lowestBytes, (unsigned)m.largestBlock, (unsigned)m.psramFree);

  if (cfg::DEMO_PRICES) return;
  FeedStats f;
  quoteClientStats(f);
  bool up = WiFi.isConnected();
  char lastQuote[24] = "none yet";
  if (f.lastQuoteOkMs) snprintf(lastQuote, sizeof(lastQuote), "%lu s ago", (unsigned long)((nowMs - f.lastQuoteOkMs) / 1000));
  char lastErr[80] = "none";
  if (f.lastError[0]) {
    snprintf(lastErr, sizeof(lastErr), "\"%s\" %lu min ago", f.lastError, (unsigned long)((nowMs - f.lastErrorMs) / 60000));
  }
  Serial.printf("health: wifi %s %d dBm, %lu drops; finnhub %lu requests since last line (%lu total), twelve data %lu "
                "today; %lu errors since last line (%lu total); last quote %s; last error %s\n",
                up ? "connected" : "DOWN", up ? (int)WiFi.RSSI() : 0, (unsigned long)wifiDropCount(),
                (unsigned long)(f.finnhubTotal - lastFinnhub), (unsigned long)f.finnhubTotal,
                (unsigned long)f.tdToday, (unsigned long)(f.errorsTotal - lastErrors), (unsigned long)f.errorsTotal,
                lastQuote, lastErr);
  lastFinnhub = f.finnhubTotal;
  lastErrors = f.errorsTotal;
}
