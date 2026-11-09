// Gets the real date and time from internet time servers (SNTP) once Wi-Fi is up.
// The ESP32 has no battery-backed clock, so until this succeeds it doesn't know the time.
#pragma once
#include "market_calendar.h"

void timeSyncStart();             // call once Wi-Fi is connected
bool timeNowET(EtTime& out);      // current US Eastern time; false until synced
bool marketIsOpenNow();           // regular session right now; false until synced

// Start (9:30 ET) and end (16:00, or 13:00 on early-close days) of the trading session
// on the same day as `anyTime`, all in unix seconds. Used to place chart points by time.
void sessionBounds(uint32_t anyTime, uint32_t& openUnix, uint32_t& closeUnix);
