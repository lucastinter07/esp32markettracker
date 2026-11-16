#include "ui/diag_screen.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ctype.h>
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
#include "ui/gfx.h"
#include "ui/palette.h"

#ifndef TWELVEDATA_API_KEY
#define TWELVEDATA_API_KEY ""
#endif

namespace diagui {

// ---------- Layout (layout A: header bar, rows, grouped tab buttons like Detail's) ----------
static const int16_t ROW_Y0 = 36, ROW_H = 26;   // DEVICE / NETWORK: up to 9 rows
static const int16_t VALUE_X = 190;
static const int16_t NOTE_Y = 266;               // one line of small text above the tabs
static const int16_t TAB_X = 8, TAB_Y = 282, TAB_H = 30, TAB_W = 154;
static const int TAB_COUNT = 3;
static const char* const TAB_NAMES[TAB_COUNT] = {"DEVICE", "NETWORK", "DATA"};
static const int MAX_ROWS = 9;

// DATA tab grid: one row per symbol, one column per kind of data
static const int16_t GRID_HEAD_Y = 36, GRID_Y0 = 52, GRID_ROW_H = 30;
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
  ui::text(x, y, s, size, fg, ui::pal().bg);
}

// Values are kept in mixed case below for readability in the code; the screen shows capitals.
static void printUpper(int16_t x, int16_t y, uint8_t size, uint16_t fg, const char* s) {
  char up[80];
  snprintf(up, sizeof(up), "%s", s);
  for (char* c = up; *c; c++) *c = (char)toupper((unsigned char)*c);
  printAt(x, y, size, fg, up);
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
  rows[n++].color = ui::pal().text;

  uint32_t up = nowMs / 60000;   // minutes
  rows[n].label = "UPTIME";
  snprintf(rows[n].value, sizeof(rows[n].value), "%lud %luh %lum", (unsigned long)(up / 1440),
           (unsigned long)(up / 60 % 24), (unsigned long)(up % 60));
  rows[n++].color = ui::pal().text;

  const char* reason = healthResetReason();
  rows[n].label = "LAST RESTART";
  snprintf(rows[n].value, sizeof(rows[n].value), "%s", reason);
  rows[n++].color = (strncmp(reason, "CRASH", 5) == 0 || strncmp(reason, "POWER", 5) == 0) ? ui::pal().down : ui::pal().text;

  MemoryInfo m;
  healthMemory(m);
  rows[n].label = "FREE MEMORY";
  kbText(rows[n].value, sizeof(rows[n].value), m.freeBytes);
  rows[n++].color = ui::pal().text;
  rows[n].label = "LOWEST FREE";
  kbText(rows[n].value, sizeof(rows[n].value), m.lowestBytes);
  rows[n++].color = ui::pal().text;
  rows[n].label = "LARGEST BLOCK";
  kbText(rows[n].value, sizeof(rows[n].value), m.largestBlock);
  // An encrypted connection needs roughly 40 KB in one piece.
  rows[n++].color = m.largestBlock < 40 * 1024 ? ui::pal().down : ui::pal().text;
  rows[n].label = "PSRAM FREE";
  if (m.psramFree > 0) kbText(rows[n].value, sizeof(rows[n].value), m.psramFree);
  else snprintf(rows[n].value, sizeof(rows[n].value), "not enabled");
  rows[n++].color = ui::pal().text;

  if (!storageRead || nowMs - storageReadMs >= STORAGE_REFRESH_MS) {
    storageInfo(storage);
    storageReadMs = nowMs;
    storageRead = true;
  }
  rows[n].label = "FLASH USED";
  if (storage.mounted) {
    snprintf(rows[n].value, sizeof(rows[n].value), "%lu of %lu KB", (unsigned long)(storage.usedBytes / 1024),
             (unsigned long)(storage.totalBytes / 1024));
    rows[n++].color = ui::pal().text;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "not mounted");
    rows[n++].color = ui::pal().down;
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
  rows[n++].color = w == WifiState::Connected ? ui::pal().up : ui::pal().target;

  bool connected = w == WifiState::Connected && WiFi.isConnected();
  rows[n].label = "NETWORK";
  snprintf(rows[n].value, sizeof(rows[n].value), "%.22s", connected ? WiFi.SSID().c_str() : "--");
  rows[n++].color = ui::pal().text;

  rows[n].label = "SIGNAL";
  if (connected) {
    int rssi = WiFi.RSSI();
    snprintf(rows[n].value, sizeof(rows[n].value), "%s (%d dBm)", signalWord(rssi), rssi);
    rows[n++].color = rssi < -80 ? ui::pal().down : ui::pal().text;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "--");
    rows[n++].color = ui::pal().text;
  }

  rows[n].label = "IP ADDRESS";
  snprintf(rows[n].value, sizeof(rows[n].value), "%s", connected ? WiFi.localIP().toString().c_str() : "--");
  rows[n++].color = ui::pal().text;

  uint32_t syncAgo;
  char age[12];
  rows[n].label = "CLOCK";
  if (timeLastSync(syncAgo)) {
    ageText(age, sizeof(age), syncAgo);
    snprintf(rows[n].value, sizeof(rows[n].value), "synced %s ago", age);
    rows[n++].color = ui::pal().text;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "not synced yet");
    rows[n++].color = ui::pal().target;
  }

  FeedStats f;
  quoteClientStats(f);
  const char* problem = quoteFeedProblem();
  FeedStatus st = quoteFeedStatus();
  rows[n].label = "FINNHUB";
  if (problem) {
    snprintf(rows[n].value, sizeof(rows[n].value), "%s", problem);
    rows[n++].color = ui::pal().down;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "%s",
             st == FeedStatus::Live ? "live" : (st == FeedStatus::Idle ? "idle (market closed)" : "starting"));
    rows[n++].color = st == FeedStatus::Starting ? ui::pal().target : ui::pal().up;
  }

  rows[n].label = "LAST QUOTE";
  if (f.lastQuoteOkMs) {
    ageText(age, sizeof(age), nowMs - f.lastQuoteOkMs);
    snprintf(rows[n].value, sizeof(rows[n].value), "%s ago, %d failed", age, f.failuresInRow);
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "none yet, %d failed", f.failuresInRow);
  }
  rows[n++].color = f.failuresInRow >= 3 ? ui::pal().down : ui::pal().text;

  rows[n].label = "REQUESTS";
  snprintf(rows[n].value, sizeof(rows[n].value), "%lu/min, %lu total", (unsigned long)f.finnhubLastMinute,
           (unsigned long)f.finnhubTotal);
  rows[n++].color = f.finnhubLastMinute >= 50 ? ui::pal().target : ui::pal().text;   // limit is 60

  rows[n].label = "TWELVE DATA";
  if (TWELVEDATA_API_KEY[0] == '\0') {
    snprintf(rows[n].value, sizeof(rows[n].value), "no key");
    rows[n++].color = ui::pal().dim;
  } else if (f.lastTdOkMs) {
    ageText(age, sizeof(age), nowMs - f.lastTdOkMs);
    snprintf(rows[n].value, sizeof(rows[n].value), "%s ago, %lu/800 today", age, (unsigned long)f.tdToday);
    rows[n++].color = f.tdToday >= 700 ? ui::pal().target : ui::pal().text;
  } else {
    snprintf(rows[n].value, sizeof(rows[n].value), "none yet, %lu today", (unsigned long)f.tdToday);
    rows[n++].color = ui::pal().text;
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
  color = ui::pal().text;
  auto pending = [&]() {
    strcpy(out, "...");
    color = ui::pal().target;
  };
  auto missing = [&]() {
    strcpy(out, "--");
    color = ui::pal().down;
  };
  auto unused = [&]() {
    strcpy(out, "-");
    color = ui::pal().faint;
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
      if (isStock && dueBit(f.gapDue, i)) color = ui::pal().target;
      break;
  }
}

// ---------- Drawing ----------
static void drawTabs() {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  gfx.drawRect(TAB_X, TAB_Y, TAB_COUNT * TAB_W + 1, TAB_H, p.edge);
  for (int i = 0; i < TAB_COUNT; i++) {
    int16_t x = TAB_X + i * TAB_W;
    if (i) gfx.drawFastVLine(x, TAB_Y, TAB_H, p.edge);
    bool on = i == tab;
    uint16_t bg = on ? p.accent : p.bg;
    gfx.fillRect(x + 1, TAB_Y + 1, TAB_W - 1, TAB_H - 2, bg);
    ui::text(x + TAB_W / 2, TAB_Y + 7, TAB_NAMES[i], 2, on ? p.accentInk : p.dim, bg, ui::Align::Center);
  }
}

static void drawNote(const char* note) {
  if (strcmp(note, drawnNote) == 0) return;
  display().fillRect(0, NOTE_Y, cfg::SCREEN_W, 8, ui::pal().bg);
  char line[80];
  snprintf(line, sizeof(line), "%.77s", note);   // 77 characters fit across at size 1
  printUpper(8, NOTE_Y, 1, ui::pal().dim, line);
  strncpy(drawnNote, note, sizeof(drawnNote) - 1);
  drawnNote[sizeof(drawnNote) - 1] = '\0';
}

// Draws (or refreshes) the current tab. full = true after clearing the body.
static void drawBody(uint32_t nowMs, bool full) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  char note[80] = "";

  if (tab < 2) {
    Row rows[MAX_ROWS];
    int n = tab == 0 ? deviceRows(rows, note, sizeof(note), nowMs) : networkRows(rows, note, sizeof(note), nowMs);
    for (int i = 0; i < n; i++) {
      int16_t y = ROW_Y0 + i * ROW_H;
      if (full) printAt(8, y, 2, p.dim, rows[i].label);
      if (full || strcmp(rows[i].value, drawnValue[i]) != 0 || rows[i].color != drawnColor[i]) {
        gfx.fillRect(VALUE_X, y, cfg::SCREEN_W - VALUE_X, 16, p.bg);
        // Capitals, except the Wi-Fi name: it's case-sensitive, so it's shown as typed.
        if (strcmp(rows[i].label, "NETWORK") == 0) printAt(VALUE_X, y, 2, rows[i].color, rows[i].value);
        else printUpper(VALUE_X, y, 2, rows[i].color, rows[i].value);
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
      printAt(8, GRID_HEAD_Y, 1, p.dim, "SYMBOL");
      for (int c = 0; c < GRID_COLS; c++) printAt(GRID_X[c], GRID_HEAD_Y, 1, p.dim, GRID_HEADS[c]);
    }
    for (int i = 0; i < NUM_SYMBOLS; i++) {
      int16_t y = GRID_Y0 + i * GRID_ROW_H;
      if (full) printAt(8, y, 2, i < cfg::NUM_TICKERS ? p.text : p.dim, symbolByIndex(i).symbol);
      for (int c = 0; c < GRID_COLS; c++) {
        char text[8];
        uint16_t color;
        cellText(i, c, f, today, nowMs, text, color);
        if (full || strcmp(text, drawnCell[i][c]) != 0 || color != drawnCellColor[i][c]) {
          gfx.fillRect(GRID_X[c], y, GRID_W, 16, p.bg);
          printUpper(GRID_X[c], y, 2, color, text);
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
  display().fillRect(0, ui::BAR_H, cfg::SCREEN_W, TAB_Y - ui::BAR_H, ui::pal().bg);
  drawBody(nowMs, true);
  drawTabs();
  lastRefreshMs = nowMs;
}

// ---------- Public ----------
void open(uint32_t nowMs) {
  display().fillScreen(ui::pal().bg);
  ui::headerBar("< BACK  DIAGNOSTICS", nullptr);
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
      if (ev.x < 140 && ev.y < ui::BAR_H + 12) return Result::Exit;
      if (ev.y >= TAB_Y - 6) {
        int t = (ev.x - TAB_X) / TAB_W;
        showTab(t < 0 ? 0 : (t >= TAB_COUNT ? TAB_COUNT - 1 : t), nowMs);
      }
      break;
    default:
      break;
  }
  return Result::Stay;
}

}  // namespace diagui
