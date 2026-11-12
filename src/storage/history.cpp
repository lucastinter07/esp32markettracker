#include "storage/history.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "data/sectors.h"
#include "market_calendar.h"
#include "net/time_sync.h"

// File layout (all numbers little-endian, as the ESP32 stores them in memory):
//   intraday.bin:     magic "MTI2", session day (int32), symbol count (uint8), then per symbol:
//                     name (8 bytes), point count (uint16), prices (float each),
//                     times (uint32 unix seconds each). (MTI1 had no times; it's ignored.)
//   fundamentals.bin: magic "MTF1", symbol count (uint8), then per symbol:
//                     name (8 bytes), Fundamentals struct (raw bytes)
//   profiles.bin:     magic "MTP1", same layout with the Profile struct
//   history.bin:      magic "MTH1", same layout with the History struct (backfill)
//   earnings.bin:     magic "MTE1", same layout with the Earnings struct
//   sectors.bin:      magic "MTS1", count (uint8), then per sector ETF (and SPY):
//                     name (8 bytes), SectorQuote struct (raw bytes)
// Symbols are matched by name, so changing the watchlist doesn't scramble saved data.
static const uint32_t MAGIC_INTRADAY = 0x3249544D;   // the bytes "MTI2"
static const uint32_t MAGIC_FUNDAMENTALS = 0x3146544D;   // "MTF1"
static const uint32_t MAGIC_PROFILES = 0x3150544D;       // "MTP1"
static const uint32_t MAGIC_HISTORY = 0x3148544D;        // "MTH1"
static const uint32_t MAGIC_EARNINGS = 0x3145544D;       // "MTE1"
static const char* INTRADAY_PATH = "/intraday.bin";
static const char* FUNDAMENTALS_PATH = "/fundamentals.bin";
static const char* PROFILES_PATH = "/profiles.bin";
static const char* HISTORY_PATH = "/history.bin";
static const char* EARNINGS_PATH = "/earnings.bin";
static const uint32_t MAGIC_SECTORS = 0x3153544D;        // "MTS1"
static const char* SECTORS_PATH = "/sectors.bin";
static const int NAME_LEN = 8;

static bool mounted = false;
static bool restored = false;
static bool wasOpen = false;
static uint32_t lastSaveMs = 0;
static uint32_t savedVersionSum = 0;

// ---------- helpers ----------

static void writeName(File& f, const char* symbol) {
  char name[NAME_LEN] = {0};
  strncpy(name, symbol, NAME_LEN - 1);
  f.write((const uint8_t*)name, NAME_LEN);
}

static SymbolState* findSymbol(const char* name) {
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    if (strncmp(symbolByIndex(i).symbol, name, NAME_LEN) == 0) return &symbolByIndex(i);
  }
  return nullptr;
}

// True if `path` (e.g. "/history.bin") exists. Reads the directory listing rather than
// calling LittleFS.exists(), because in this core exists() tries to open the file and
// logs an error whenever it's missing.
static bool fileExists(const char* path) {
  File dir = LittleFS.open("/");
  if (!dir) return false;
  bool found = false;
  for (File f = dir.openNextFile(); f && !found; f = dir.openNextFile()) {
    const char* name = f.name();   // may or may not include the leading "/"
    found = strcmp(name, path) == 0 || strcmp(name, path + 1) == 0;
    f.close();
  }
  dir.close();
  return found;
}

// Writes to a temporary file, then swaps it in, so a power cut mid-save can never
// leave a half-written file behind.
static void replaceFile(const char* tmpPath, const char* path) {
  if (fileExists(path)) LittleFS.remove(path);   // checking first avoids a noisy error log
  LittleFS.rename(tmpPath, path);
}

// Changes whenever any symbol updates; used to skip saves when nothing is new.
static uint32_t versionSum() {
  uint32_t sum = 0;
  for (int i = 0; i < NUM_SYMBOLS; i++) sum += symbolByIndex(i).version;
  return sum;
}

// ---------- chart points ----------

static void saveIntraday(int32_t sessionDay) {
  File f = LittleFS.open("/intraday.tmp", "w");
  if (!f) return;
  uint8_t count = NUM_SYMBOLS;
  f.write((const uint8_t*)&MAGIC_INTRADAY, 4);
  f.write((const uint8_t*)&sessionDay, 4);
  f.write(&count, 1);
  static float points[PriceSeries::CAPACITY];   // static: 1.5 KB each is too much for the stack
  static uint32_t times[PriceSeries::CAPACITY];
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    const PriceSeries& series = symbolByIndex(i).intraday;
    uint16_t n = series.size();
    for (uint16_t k = 0; k < n; k++) {
      points[k] = series.at(k);
      times[k] = series.timeAt(k);
    }
    writeName(f, symbolByIndex(i).symbol);
    f.write((const uint8_t*)&n, 2);
    f.write((const uint8_t*)points, n * sizeof(float));
    f.write((const uint8_t*)times, n * sizeof(uint32_t));
  }
  f.close();
  replaceFile("/intraday.tmp", INTRADAY_PATH);
}

static void loadIntraday(int32_t sessionDay) {
  if (!fileExists(INTRADAY_PATH)) return;   // nothing saved yet
  File f = LittleFS.open(INTRADAY_PATH, "r");
  if (!f) return;
  uint32_t magic = 0;
  int32_t day = 0;
  uint8_t count = 0;
  f.read((uint8_t*)&magic, 4);
  f.read((uint8_t*)&day, 4);
  f.read(&count, 1);
  if (magic != MAGIC_INTRADAY || day != sessionDay) {
    f.close();
    Serial.println("storage: saved chart is from an older session, not restored");
    return;
  }
  static float points[PriceSeries::CAPACITY];
  static uint32_t times[PriceSeries::CAPACITY];
  int restoredSymbols = 0;
  for (int i = 0; i < count; i++) {
    char name[NAME_LEN];
    uint16_t n = 0;
    if (f.read((uint8_t*)name, NAME_LEN) != NAME_LEN || f.read((uint8_t*)&n, 2) != 2) break;
    if (n > PriceSeries::CAPACITY) break;   // not a file we wrote
    uint16_t keep = n;
    f.read((uint8_t*)points, keep * sizeof(float));
    f.read((uint8_t*)times, keep * sizeof(uint32_t));
    SymbolState* s = findSymbol(name);
    // Only replace if the saved chart is longer than what has arrived since boot.
    if (s && keep > s->intraday.size()) {
      s->intraday.clear();
      for (uint16_t k = 0; k < keep; k++) s->intraday.push(points[k], times[k]);
      s->version++;
      restoredSymbols++;
    }
  }
  f.close();
  Serial.printf("storage: restored chart points for %d symbols\n", restoredSymbols);
}

// ---------- company stats and profiles ----------
//
// Both are one fixed-size struct per symbol, so one pair of functions handles both.
// `T` is the struct type (Fundamentals or Profile). `field` is a pointer-to-member: it
// names which member of SymbolState to use, e.g. &SymbolState::profile, and
// `symbol.*field` then means "that member of this symbol".

template <typename T>
static void saveRecords(const char* path, uint32_t magic, T SymbolState::*field) {
  if (!mounted) return;
  String tmpPath = String(path) + ".tmp";
  File f = LittleFS.open(tmpPath, "w");
  if (!f) return;
  uint8_t count = NUM_SYMBOLS;
  f.write((const uint8_t*)&magic, 4);
  f.write(&count, 1);
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    writeName(f, symbolByIndex(i).symbol);
    f.write((const uint8_t*)&(symbolByIndex(i).*field), sizeof(T));
  }
  f.close();
  replaceFile(tmpPath.c_str(), path);
}

// Returns how many symbols got a record, for the startup log.
template <typename T>
static int loadRecords(const char* path, uint32_t magic, T SymbolState::*field) {
  if (!fileExists(path)) return 0;
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  uint32_t fileMagic = 0;
  uint8_t count = 0;
  f.read((uint8_t*)&fileMagic, 4);
  f.read(&count, 1);
  if (fileMagic != magic) {
    f.close();
    return 0;
  }
  int loaded = 0;
  for (int i = 0; i < count; i++) {
    char name[NAME_LEN];
    T record;
    if (f.read((uint8_t*)name, NAME_LEN) != NAME_LEN) break;
    if (f.read((uint8_t*)&record, sizeof(T)) != sizeof(T)) break;
    SymbolState* s = findSymbol(name);
    if (s && record.valid) {
      s->*field = record;
      s->version++;
      loaded++;
    }
  }
  f.close();
  return loaded;
}

void storageSaveFundamentals() {
  saveRecords(FUNDAMENTALS_PATH, MAGIC_FUNDAMENTALS, &SymbolState::fundamentals);
}

void storageSaveProfiles() {
  saveRecords(PROFILES_PATH, MAGIC_PROFILES, &SymbolState::profile);
}

void storageSaveHistory() {
  saveRecords(HISTORY_PATH, MAGIC_HISTORY, &SymbolState::history);
}

void storageSaveCharts() {
  EtTime now;
  if (!mounted || cfg::DEMO_PRICES || !timeNowET(now)) return;
  saveIntraday(lastSessionDay(now));
}

void storageSaveEarnings() {
  saveRecords(EARNINGS_PATH, MAGIC_EARNINGS, &SymbolState::earnings);
}

// ---------- sector heat map ----------
// The latest quote per sector ETF, so the map shows the last close right after a restart
// while the market is closed (no new quotes until the boot pass gets through).

void storageSaveSectors() {
  if (!mounted || cfg::DEMO_PRICES) return;
  File f = LittleFS.open("/sectors.tmp", "w");
  if (!f) return;
  uint8_t count = NUM_SECTOR_QUOTES;
  f.write((const uint8_t*)&MAGIC_SECTORS, 4);
  f.write(&count, 1);
  for (int i = 0; i < NUM_SECTOR_QUOTES; i++) {
    writeName(f, sectorSymbol(i));
    f.write((const uint8_t*)&sectorQuote(i), sizeof(SectorQuote));
  }
  f.close();
  replaceFile("/sectors.tmp", SECTORS_PATH);
}

static int loadSectors() {
  if (!fileExists(SECTORS_PATH)) return 0;
  File f = LittleFS.open(SECTORS_PATH, "r");
  if (!f) return 0;
  uint32_t magic = 0;
  uint8_t count = 0;
  f.read((uint8_t*)&magic, 4);
  f.read(&count, 1);
  int loaded = 0;
  for (int k = 0; magic == MAGIC_SECTORS && k < count; k++) {
    char name[NAME_LEN];
    SectorQuote q;
    if (f.read((uint8_t*)name, NAME_LEN) != NAME_LEN) break;
    if (f.read((uint8_t*)&q, sizeof(q)) != sizeof(q)) break;
    for (int i = 0; i < NUM_SECTOR_QUOTES; i++) {
      if (strncmp(sectorSymbol(i), name, NAME_LEN) == 0) {
        sectorsRestore(i, q);
        loaded++;
      }
    }
  }
  f.close();
  return loaded;
}

// ---------- public ----------

void storageInit() {
  // true = format the partition if it has never been used (first boot)
  mounted = LittleFS.begin(true);
  Serial.println(mounted ? "storage: flash file system ready" : "storage: flash file system failed");
}

void storageUpdate(uint32_t nowMs) {
  if (!mounted || cfg::DEMO_PRICES) return;   // never save made-up prices
  EtTime now;
  if (!timeNowET(now)) return;   // need the date to know which session the data belongs to

  if (!restored) {
    loadIntraday(lastSessionDay(now));
    int stats = loadRecords(FUNDAMENTALS_PATH, MAGIC_FUNDAMENTALS, &SymbolState::fundamentals);
    int names = loadRecords(PROFILES_PATH, MAGIC_PROFILES, &SymbolState::profile);
    int history = loadRecords(HISTORY_PATH, MAGIC_HISTORY, &SymbolState::history);
    loadRecords(EARNINGS_PATH, MAGIC_EARNINGS, &SymbolState::earnings);
    int sectors = loadSectors();
    Serial.printf("storage: restored names %d, stats %d, history %d (of %d stocks), sectors %d\n", names,
                  stats, history, cfg::NUM_TICKERS, sectors);
    restored = true;
  }

  bool open = marketStatus(now).phase == MarketPhase::Open;
  uint32_t sum = versionSum();
  bool due = open && nowMs - lastSaveMs >= cfg::HISTORY_SAVE_INTERVAL_S * 1000 && sum != savedVersionSum;
  bool justClosed = wasOpen && !open;
  if (due || justClosed) {
    saveIntraday(lastSessionDay(now));
    if (justClosed) storageSaveFundamentals();   // keeps the day's 52-week updates
    lastSaveMs = nowMs;
    savedVersionSum = sum;
  }
  wasOpen = open;
}

bool storageRestored() {
  return restored || cfg::DEMO_PRICES;
}

// Size of a file from the directory listing (0 if it isn't there), for the same reason as
// fileExists(): opening a missing file logs an error.
static uint32_t fileSize(const char* path) {
  File dir = LittleFS.open("/");
  if (!dir) return 0;
  uint32_t size = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* name = f.name();
    if (strcmp(name, path) == 0 || strcmp(name, path + 1) == 0) size = f.size();
    f.close();
  }
  dir.close();
  return size;
}

void storageInfo(StorageInfo& out) {
  // Short names: the whole list shares one 80-character line on the Diagnostics screen.
  static const char* const NAMES[STORAGE_FILES] = {"chart", "stats", "names", "hist", "earn", "sect"};
  static const char* const PATHS[STORAGE_FILES] = {INTRADAY_PATH, FUNDAMENTALS_PATH, PROFILES_PATH,
                                                   HISTORY_PATH, EARNINGS_PATH, SECTORS_PATH};
  out.mounted = mounted;
  out.usedBytes = mounted ? LittleFS.usedBytes() : 0;
  out.totalBytes = mounted ? LittleFS.totalBytes() : 0;
  for (int i = 0; i < STORAGE_FILES; i++) {
    out.names[i] = NAMES[i];
    out.sizes[i] = mounted ? fileSize(PATHS[i]) : 0;
  }
}
