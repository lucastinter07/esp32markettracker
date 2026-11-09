#include "storage/history.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "market_calendar.h"
#include "net/time_sync.h"

// File layout (all numbers little-endian, as the ESP32 stores them in memory):
//   intraday.bin:     magic "MTI1", session day (int32), symbol count (uint8), then per symbol:
//                     name (8 bytes), point count (uint16), points (float each)
//   fundamentals.bin: magic "MTF1", symbol count (uint8), then per symbol:
//                     name (8 bytes), Fundamentals struct (raw bytes)
//   profiles.bin:     magic "MTP1", same layout with the Profile struct
// Symbols are matched by name, so changing the watchlist doesn't scramble saved data.
static const uint32_t MAGIC_INTRADAY = 0x3149544D;   // the bytes "MTI1"
static const uint32_t MAGIC_FUNDAMENTALS = 0x3146544D;   // "MTF1"
static const uint32_t MAGIC_PROFILES = 0x3150544D;       // "MTP1"
static const char* INTRADAY_PATH = "/intraday.bin";
static const char* FUNDAMENTALS_PATH = "/fundamentals.bin";
static const char* PROFILES_PATH = "/profiles.bin";
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

// Writes to a temporary file, then swaps it in, so a power cut mid-save can never
// leave a half-written file behind.
static void replaceFile(const char* tmpPath, const char* path) {
  if (LittleFS.exists(path)) LittleFS.remove(path);   // checking first avoids a noisy error log
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
  static float points[PriceSeries::CAPACITY];   // static: 1.5 KB is too much for the stack
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    const PriceSeries& series = symbolByIndex(i).intraday;
    uint16_t n = series.size();
    for (uint16_t k = 0; k < n; k++) points[k] = series.at(k);
    writeName(f, symbolByIndex(i).symbol);
    f.write((const uint8_t*)&n, 2);
    f.write((const uint8_t*)points, n * sizeof(float));
  }
  f.close();
  replaceFile("/intraday.tmp", INTRADAY_PATH);
}

static void loadIntraday(int32_t sessionDay) {
  if (!LittleFS.exists(INTRADAY_PATH)) return;   // nothing saved yet
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
  int restoredSymbols = 0;
  for (int i = 0; i < count; i++) {
    char name[NAME_LEN];
    uint16_t n = 0;
    if (f.read((uint8_t*)name, NAME_LEN) != NAME_LEN || f.read((uint8_t*)&n, 2) != 2) break;
    uint16_t keep = n > PriceSeries::CAPACITY ? PriceSeries::CAPACITY : n;
    f.read((uint8_t*)points, keep * sizeof(float));
    if (keep < n) f.seek((n - keep) * sizeof(float), SeekCur);
    SymbolState* s = findSymbol(name);
    // Only replace if the saved chart is longer than what has arrived since boot.
    if (s && keep > s->intraday.size()) {
      s->intraday.clear();
      for (uint16_t k = 0; k < keep; k++) s->intraday.push(points[k]);
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

template <typename T>
static void loadRecords(const char* path, uint32_t magic, T SymbolState::*field) {
  if (!LittleFS.exists(path)) return;
  File f = LittleFS.open(path, "r");
  if (!f) return;
  uint32_t fileMagic = 0;
  uint8_t count = 0;
  f.read((uint8_t*)&fileMagic, 4);
  f.read(&count, 1);
  if (fileMagic != magic) {
    f.close();
    return;
  }
  for (int i = 0; i < count; i++) {
    char name[NAME_LEN];
    T record;
    if (f.read((uint8_t*)name, NAME_LEN) != NAME_LEN) break;
    if (f.read((uint8_t*)&record, sizeof(T)) != sizeof(T)) break;
    SymbolState* s = findSymbol(name);
    if (s && record.valid) {
      s->*field = record;
      s->version++;
    }
  }
  f.close();
}

void storageSaveFundamentals() {
  saveRecords(FUNDAMENTALS_PATH, MAGIC_FUNDAMENTALS, &SymbolState::fundamentals);
}

void storageSaveProfiles() {
  saveRecords(PROFILES_PATH, MAGIC_PROFILES, &SymbolState::profile);
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
    loadRecords(FUNDAMENTALS_PATH, MAGIC_FUNDAMENTALS, &SymbolState::fundamentals);
    loadRecords(PROFILES_PATH, MAGIC_PROFILES, &SymbolState::profile);
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
