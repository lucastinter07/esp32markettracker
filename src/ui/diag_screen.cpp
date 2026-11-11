#include "ui/diag_screen.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "app/health.h"
#include "config.h"
#include "data/market_data.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "storage/history.h"
#include "ui/theme.h"

#ifndef TWELVEDATA_API_KEY
#define TWELVEDATA_API_KEY ""
#endif

namespace diagui {

// ---------- Layout ----------
static const int16_t HEADER_H = 40;
static const int16_t BACK_W = 120;
static const int16_t ROW_Y0 = 50, ROW_H = 24;   // DEVICE / NETWORK: up to 9 rows
static const int16_t VALUE_X = 190;
static const int16_t NOTE_Y = 272;               // one line of small text above the tabs
static const int16_t TAB_Y = 288, TAB_H = 32;
static const int TAB_COUNT = 3;
static const char* const TAB_NAMES[TAB_COUNT] = {"DEVICE", "NETWORK", "DATA"};
static const int MAX_ROWS = 9;

// DATA tab grid: one row per symbol, one column per kind of data
static const int16_t GRID_HEAD_Y = 52, GRID_Y0 = 68, GRID_ROW_H = 28;
static const int GRID_COLS = 6;
static const char* const GRID_HEADS[GRID_COLS] = {"PRICE", "NAME", "STATS", "HISTORY", "EARN", "CHART"};
static const int16_t GRID_X[GRID_COLS] = {100, 160, 215, 280, 350, 410};
static const int16_t GRID_W = 56;   // width cleared per cell

static const uint32_t REFRESH_MS = 1000;
static const uint32_t STORAGE_REFRESH_MS = 10000;   // reading the file list is slower

// ---------- State ----------
static int tab = 0;
static uint32_t lastRefreshMs = 0;
static char drawnValue[MAX_ROWS][32];
static uint16_t drawnColor[MAX_ROWS];
static char drawnNote[80];
static char drawnCell[NUM_SYMBOLS][GRID_COLS][8];
static uint16_t drawnCellColor[NUM_SYMBOLS][GRID_COLS];
static StorageInfo storage;
static uint32_t storageReadMs = 0;
static bool storageRead = false;

// ---------- Small helpers ----------
static void printAt(int16_t x, int16_t y, uint8_t size, uint16_t fg, const char* s) {
  DFRobot_GDL& gfx = display();
  gfx.setTextSize(size);
  gfx.setTextColor(fg, theme::BG);
  gfx.setCursor(x, y);
  gfx.print(s);
}

// "8s", "12m", "3h", "2d": how long ago, in the biggest whole unit.
static void ageText(char* buf, size_t size, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) snprintf(buf, size, "%lus", (unsigned long)s);
  else if (s < 3600) snprintf(buf, size, "%lum", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(buf, size, "%luh", (unsigned long)(s / 3600));
  else snprintf(buf, size, "%lud", (unsigned long)(s / 86400));
}

static void kbText(char* buf, size_t size, uint32_t bytes) {
  if (bytes >= 1024u * 1024u) snprintf(buf, size, "%.1f MB", bytes / 1048576.0f);
  else snprintf(buf, size, "%.1f KB", bytes / 1024.0f);
}

static const char* signalWord(int rssi) {
  if (rssi >= -60) return "strong";
  if (rssi >= -70) return "good";
  if (rssi >= -80) return "fair";
  return "weak";
}

static bool dueBit(uint8_t mask, int i) {
  return (mask >> i) & 1u;
}

// ---------- DEVICE and NETWORK tabs: label + value rows ----------
struct Row {
  const char* label;
  char value[32];
  uint16_t color;
};

static int deviceRows(Row* rows, char* note, size_t noteSize, uint32_t nowMs) {
  int n = 0;
  rows[n].label = "FIRMWARE";
  snprintf(rows[n].value, sizeof(rows[n].value), "%s, %s", cfg::FIRMWARE_VERSION, __DATE__);
  rows[n++].color = theme::TEXT;

  uint32_t up = nowMs / 60000;   // minutes
  rows[n].label = "UPTIME";
  snprintf(rows[n].value, sizeof(rows[n].value), "%lud %luh %lum", (unsigned long)(up / 1440),
           (unsigned long)(up / 60 % 24), (unsigned long)(up % 60));
  rows[n++].color = theme::TEXT;

  const char* reason = healthResetReason();
  rows[n].label = "LAST RESTART";
  snprintf(rows[n].value, sizeof(rows[n].value), "%s", reason);
  rows[n++].color = (strncmp(reason, "CRASH", 5) == 0 || strncmp(reason, "POWER", 5) == 0) ? theme::DOWN : theme::TEXT;

  MemoryInfo m;
  healthMemory(m);
  rows[n].label = "FREE MEMORY";
  kbText(rows[n].value, sizeof(rows[n].value), m.freeBytes);
  rows[n++].color = theme::TEXT;
  rows[n].label = "LOWEST FREE";
  kbText(rows[n].value, sizeof(rows[n].value), m.lowestBytes);
  rows[n++].color = theme::TEXT;
  rows[n].label = "LARGEST BLOCK";
  kbText(rows[n].value, sizeof(rows[n].value), m.largestBlock);
  // An encrypted connection needs roughly 40 KB in one piece.
  rows[n++].color = m.largestBlock < 40 * 1024 ? theme::DOWN : theme::TEXT;
  rows[n].label = "PSRAM FREE";
  if (m.psramFree > 0) kbText(rows[n].value, sizeof(rows[n].value), m.psramFree);
  else snprintf(rows[n].value, sizeof(rows[n].value), "not enabled");
  rows[n++].color = theme::TEXT;

  if (!storageRead || nowMs - storageReadMs >= STORAGE_REFRESH_MS) {
    storageInfo(storage);
    storageReadMs = nowMs;
    storageRead = true;
  }
  rows[n].label = "FLASH USED";
  if (storage.mounted) {
    snprintf(rows[n].value, sizeof(rows[n].value), "%lu of %lu KB", (unsigned long)(storage.usedBytes / 1024),
             (unsigned long)(storage.totalBytes / 1024));
    rows[n++].color = theme::TEXT;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "not mounted");
    rows[n++].color = theme::DOWN;
  }

  // Note line: each saved file's size
  size_t used = snprintf(note, noteSize, "files:");
  for (int i = 0; i < STORAGE_FILES && used < noteSize; i++) {
    used += snprintf(note + used, noteSize - used, "  %s %.1fK", storage.names[i], storage.sizes[i] / 1024.0f);
  }
  return n;
}

static int networkRows(Row* rows, char* note, size_t noteSize, uint32_t nowMs) {
  int n = 0;
  WifiState w = wifiState();
  rows[n].label = "WI-FI";
  snprintf(rows[n].value, sizeof(rows[n].value), "%s",
           w == WifiState::Connected ? "connected" : (w == WifiState::Connecting ? "connecting" : "setup hotspot on"));
  rows[n++].color = w == WifiState::Connected ? theme::UP : theme::ACCENT;

  bool connected = w == WifiState::Connected && WiFi.isConnected();
  rows[n].label = "NETWORK";
  snprintf(rows[n].value, sizeof(rows[n].value), "%.22s", connected ? WiFi.SSID().c_str() : "--");
  rows[n++].color = theme::TEXT;

  rows[n].label = "SIGNAL";
  if (connected) {
    int rssi = WiFi.RSSI();
    snprintf(rows[n].value, sizeof(rows[n].value), "%s (%d dBm)", signalWord(rssi), rssi);
    rows[n++].color = rssi < -80 ? theme::DOWN : theme::TEXT;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "--");
    rows[n++].color = theme::TEXT;
  }

  rows[n].label = "IP ADDRESS";
  snprintf(rows[n].value, sizeof(rows[n].value), "%s", connected ? WiFi.localIP().toString().c_str() : "--");
  rows[n++].color = theme::TEXT;

  uint32_t syncAgo;
  char age[12];
  rows[n].label = "CLOCK";
  if (timeLastSync(syncAgo)) {
    ageText(age, sizeof(age), syncAgo);
    snprintf(rows[n].value, sizeof(rows[n].value), "synced %s ago", age);
    rows[n++].color = theme::TEXT;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "not synced yet");
    rows[n++].color = theme::ACCENT;
  }

  FeedStats f;
  quoteClientStats(f);
  const char* problem = quoteFeedProblem();
  FeedStatus st = quoteFeedStatus();
  rows[n].label = "FINNHUB";
  if (problem) {
    snprintf(rows[n].value, sizeof(rows[n].value), "%s", problem);
    rows[n++].color = theme::DOWN;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "%s",
             st == FeedStatus::Live ? "live" : (st == FeedStatus::Idle ? "idle (market closed)" : "starting"));
    rows[n++].color = st == FeedStatus::Starting ? theme::ACCENT : theme::UP;
  }

  rows[n].label = "LAST QUOTE";
  if (f.lastQuoteOkMs) {
    ageText(age, sizeof(age), nowMs - f.lastQuoteOkMs);
    snprintf(rows[n].value, sizeof(rows[n].value), "%s ago, %d failed", age, f.failuresInRow);
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "none yet, %d failed", f.failuresInRow);
  }
  rows[n++].color = f.failuresInRow >= 3 ? theme::DOWN : theme::TEXT;

  rows[n].label = "REQUESTS";
  snprintf(rows[n].value, sizeof(rows[n].value), "%lu/min, %lu total", (unsigned long)f.finnhubLastMinute,
           (unsigned long)f.finnhubTotal);
  rows[n++].color = f.finnhubLastMinute >= 50 ? theme::ACCENT : theme::TEXT;   // limit is 60

  rows[n].label = "TWELVE DATA";
  if (TWELVEDATA_API_KEY[0] == '\0') {
    snprintf(rows[n].value, sizeof(rows[n].value), "no key");
    rows[n++].color = theme::LABEL;
  } else if (f.lastTdOkMs) {
    ageText(age, sizeof(age), nowMs - f.lastTdOkMs);
    snprintf(rows[n].value, sizeof(rows[n].value), "%s ago, %lu/800 today", age, (unsigned long)f.tdToday);
    rows[n++].color = f.tdToday >= 700 ? theme::ACCENT : theme::TEXT;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "none yet, %lu today", (unsigned long)f.tdToday);
    rows[n++].color = theme::TEXT;
  }

  if (f.lastError[0]) {
    ageText(age, sizeof(age), nowMs - f.lastErrorMs);
    snprintf(note, noteSize, "last problem (%s ago): %s", age, f.lastError);
  } else {
    snprintf(note, noteSize, "no network problems since boot");
  }
  return n;
}

// ---------- DATA tab: one cell per symbol and kind of data ----------
// "ok" loaded, "..." waiting to download, "--" missing, "-" not used for this symbol.
static void cellText(int i, int col, const FeedStats& f, int32_t today, uint32_t nowMs, char* out, uint16_t& color) {
  const SymbolState& s = symbolByIndex(i);
  bool isStock = i < cfg::NUM_TICKERS;
  bool hasTd = TWELVEDATA_API_KEY[0] != '\0';
  color = theme::TEXT;
  auto pending = [&]() {
    strcpy(out, "...");
    color = theme::ACCENT;
  };
  auto missing = [&]() {
    strcpy(out, "--");
    color = theme::DOWN;
  };
  auto unused = [&]() {
    strcpy(out, "-");
    color = theme::DIM;
  };
  switch (col) {
    case 0:   // price: how long ago it last updated
      if (dueBit(f.quoteDue, i)) pending();
      else if (!s.hasData) missing();
      else ageText(out, 8, nowMs - s.lastUpdateMs);
      break;
    case 1:   // company name
      if (!isStock) unused();
      else if (dueBit(f.profilesDue, i)) pending();
      else if (s.profile.valid) strcpy(out, "ok");
      else missing();
      break;
    case 2:   // company stats: days since the download
      if (!isStock) unused();
      else if (dueBit(f.metricsDue, i)) pending();
      else if (s.fundamentals.valid) snprintf(out, 8, "%ldd", (long)(today - s.fundamentals.fetchedDay));
      else missing();
      break;
    case 3:   // Twelve Data history
      if (!isStock || !hasTd) unused();
      else if (dueBit(f.historyDue, i)) pending();
      else if (s.history.valid) strcpy(out, "ok");
      else missing();
      break;
    case 4:   // next earnings date
      if (!isStock) unused();
      else if (dueBit(f.earningsDue, i)) pending();
      else if (s.earnings.valid) strcpy(out, "ok");
      else missing();
      break;
    default:   // chart: number of points today (amber while a hole is being filled)
      snprintf(out, 8, "%u", (unsigned)s.intraday.size());
      if (isStock && dueBit(f.gapDue, i)) color = theme::ACCENT;
      break;
  }
}

// ---------- Drawing ----------
static void drawTabs() {
  DFRobot_GDL& gfx = display();
  int16_t w = cfg::SCREEN_W / TAB_COUNT;
  gfx.setTextSize(2);
  for (int i = 0; i < TAB_COUNT; i++) {
    bool selected = i == tab;
    uint16_t bg = selected ? theme::HIGHLIGHT : theme::PANEL;
    gfx.fillRect(i * w, TAB_Y, w - 1, TAB_H, bg);
    gfx.setTextColor(selected ? theme::TEXT : theme::LABEL, bg);
    gfx.setCursor(i * w + (w - (int16_t)strlen(TAB_NAMES[i]) * 12) / 2, TAB_Y + 9);
    gfx.print(TAB_NAMES[i]);
  }
}

static void drawNote(const char* note) {
  if (strcmp(note, drawnNote) == 0) return;
  display().fillRect(0, NOTE_Y, cfg::SCREEN_W, 10, theme::BG);
  char line[80];
  snprintf(line, sizeof(line), "%.78s", note);   // 78 characters fit across at size 1
  printAt(10, NOTE_Y, 1, theme::LABEL, line);
  strncpy(drawnNote, note, sizeof(drawnNote) - 1);
  drawnNote[sizeof(drawnNote) - 1] = '\0';
}

// Draws (or refreshes) the current tab. full = true after clearing the body.
static void drawBody(uint32_t nowMs, bool full) {
  DFRobot_GDL& gfx = display();
  char note[80] = "";

  if (tab < 2) {
    Row rows[MAX_ROWS];
    int n = tab == 0 ? deviceRows(rows, note, sizeof(note), nowMs) : networkRows(rows, note, sizeof(note), nowMs);
    for (int i = 0; i < n; i++) {
      int16_t y = ROW_Y0 + i * ROW_H;
      if (full) printAt(10, y, 2, theme::STAT_LABEL, rows[i].label);
      if (full || strcmp(rows[i].value, drawnValue[i]) != 0 || rows[i].color != drawnColor[i]) {
        gfx.fillRect(VALUE_X, y, cfg::SCREEN_W - VALUE_X, 16, theme::BG);
        printAt(VALUE_X, y, 2, rows[i].color, rows[i].value);
        strcpy(drawnValue[i], rows[i].value);
        drawnColor[i] = rows[i].color;
      }
    }
  } else {
    FeedStats f;
    quoteClientStats(f);
    EtTime now;
    int32_t today = timeNowET(now) ? dayNumber(now.year, now.month, now.day) : 0;
    if (full) {
      printAt(10, GRID_HEAD_Y, 1, theme::STAT_LABEL, "SYMBOL");
      for (int c = 0; c < GRID_COLS; c++) printAt(GRID_X[c], GRID_HEAD_Y, 1, theme::STAT_LABEL, GRID_HEADS[c]);
    }
    for (int i = 0; i < NUM_SYMBOLS; i++) {
      int16_t y = GRID_Y0 + i * GRID_ROW_H;
      if (full) printAt(10, y, 2, i < cfg::NUM_TICKERS ? theme::TEXT : theme::LABEL, symbolByIndex(i).symbol);
      for (int c = 0; c < GRID_COLS; c++) {
        char text[8];
        uint16_t color;
        cellText(i, c, f, today, nowMs, text, color);
        if (full || strcmp(text, drawnCell[i][c]) != 0 || color != drawnCellColor[i][c]) {
          gfx.fillRect(GRID_X[c], y, GRID_W, 16, theme::BG);
          printAt(GRID_X[c], y, 2, color, text);
          strcpy(drawnCell[i][c], text);
          drawnCellColor[i][c] = color;
        }
      }
    }
    snprintf(note, sizeof(note), "ok = loaded   ... = downloading   -- = missing   - = not used (index ETF)");
  }
  if (full) drawnNote[0] = '\x01';   // force the note to redraw
  drawNote(note);
}

static void showTab(int newTab, uint32_t nowMs) {
  tab = (newTab + TAB_COUNT) % TAB_COUNT;
  display().fillRect(0, HEADER_H, cfg::SCREEN_W, TAB_Y - HEADER_H, theme::BG);
  drawBody(nowMs, true);
  drawTabs();
  lastRefreshMs = nowMs;
}

// ---------- Public ----------
void open(uint32_t nowMs) {
  DFRobot_GDL& gfx = display();
  gfx.fillScreen(theme::BG);
  gfx.fillRect(0, 0, cfg::SCREEN_W, HEADER_H, theme::PANEL);
  gfx.fillRect(0, 0, BACK_W, HEADER_H, theme::HIGHLIGHT);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::TEXT, theme::HIGHLIGHT);
  gfx.setCursor(12, 12);
  gfx.print("< BACK");
  gfx.setTextSize(3);
  gfx.setTextColor(theme::TEXT, theme::PANEL);
  gfx.setCursor(BACK_W + 20, 9);
  gfx.print("DIAGNOSTICS");
  storageRead = false;
  showTab(0, nowMs);
}

void update(uint32_t nowMs) {
  if (nowMs - lastRefreshMs < REFRESH_MS) return;
  lastRefreshMs = nowMs;
  drawBody(nowMs, false);
}

Result handle(const InputEvent& ev, uint32_t nowMs) {
  switch (ev.type) {
    case InputType::Back:
      return Result::Exit;
    case InputType::Prev:
      showTab(tab - 1, nowMs);
      break;
    case InputType::Next:
    case InputType::Select:
      showTab(tab + 1, nowMs);
      break;
    case InputType::Tap:
      if (ev.x < BACK_W + 30 && ev.y < HEADER_H + 4) return Result::Exit;
      if (ev.y >= TAB_Y - 8) showTab(ev.x / (cfg::SCREEN_W / TAB_COUNT), nowMs);
      break;
    default:
      break;
  }
  return Result::Stay;
}

}  // namespace diagui
