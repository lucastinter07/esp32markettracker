// Keeps price history and company stats across restarts, in the flash file system
// (LittleFS, a small file system in part of the ESP32's flash).
//
// - Chart points: saved every cfg::HISTORY_SAVE_INTERVAL_S while the market is open and
//   once at the close. At boot, restored if they belong to the most recent session, so
//   a restart keeps today's chart and the weekend still shows Friday's.
// - Company stats and names: saved whenever they change, restored at boot, so the
//   ~196 KB metrics download only happens about once a week and names about monthly.
//
// Only the main loop uses this module (it owns market data).
#pragma once
#include <stdint.h>

void storageInit();
void storageUpdate(uint32_t nowMs);   // every loop: restores once the clock is set, then saves on schedule
bool storageRestored();               // true once saved data has been loaded (or there was none)
void storageSaveFundamentals();       // call after new company stats arrive
void storageSaveProfiles();           // call after new company names arrive
