// Gets the real date and time from internet time servers (SNTP) once Wi-Fi is up.
// The ESP32 has no battery-backed clock, so until this succeeds it doesn't know the time.
#pragma once
#include "market_calendar.h"

void timeSyncStart();             // call once Wi-Fi is connected
bool timeNowET(EtTime& out);      // current US Eastern time; false until synced
