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
void storageSaveHistory();            // call after backfilled history arrives
void storageSaveEarnings();           // call after earnings dates arrive
void storageSaveCharts();             // save the day charts now (e.g. after a gap-fill)
void storageSaveSectors();            // after a full pass of sector quotes (heat map, 8A)

// For the Diagnostics screen: flash use and the size of each saved file (0 = not saved yet).
static const int STORAGE_FILES = 6;
struct StorageInfo {
  bool mounted;
  uint32_t usedBytes, totalBytes;
  const char* names[STORAGE_FILES];   // "chart", "stats", "names", "hist", "earn", "sect"
  uint32_t sizes[STORAGE_FILES];
};
void storageInfo(StorageInfo& out);
