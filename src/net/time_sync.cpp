#include "net/time_sync.h"
#include <Arduino.h>
#include <time.h>

// POSIX time zone rule for US Eastern: EST is UTC-5, EDT starts the 2nd Sunday of March
// and ends the 1st Sunday of November. The C library applies daylight saving itself.
static const char* TZ_EASTERN = "EST5EDT,M3.2.0,M11.1.0";
static bool started = false;

void timeSyncStart() {
  if (started) return;
  configTzTime(TZ_EASTERN, "pool.ntp.org", "time.nist.gov");   // syncs in the background
  started = true;
}

bool timeNowET(EtTime& out) {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;   // still near 1970: not synced yet

  struct tm t;
  localtime_r(&now, &t);   // converts to Eastern using the rule above
  out.year = t.tm_year + 1900;
  out.month = t.tm_mon + 1;
  out.day = t.tm_mday;
  out.hour = t.tm_hour;
  out.minute = t.tm_min;
  return true;
}
