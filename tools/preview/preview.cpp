// Screen previews on the PC (Phase 9): compiles the real screen code against a stand-in
// display (stubs/DFRobot_GDL.h) and sample market data, and saves each screen as a
// 480 x 320 BMP. Lets layouts be checked for clipping/overlap without flashing the board.
//
//   tools/preview/build.sh <output folder> [theme 0-4]
//
// The data below is made up (same sample values as the design canvas).
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app/price_alerts.h"
#include "config.h"
#include "data/market_data.h"
#include "data/sectors.h"
#include "data/settings.h"
#include "data/targets.h"
#include "data/watchlist.h"
#include "hal/display.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "app/health.h"
#include "storage/history.h"
#include "ui/alert_banner.h"
#include "ui/clock_screen.h"
#include "ui/detail_screen.h"
#include "ui/diag_screen.h"
#include "ui/home_screen.h"
#include "ui/sector_screen.h"
#include "ui/settings_screen.h"
#include "ui/setup_screen.h"
#include "ui/splash_screen.h"
#include "ui/watchlist_screen.h"
#include <WiFi.h>

// ---------- stand-ins for the firmware modules the screens read ----------
PreviewSerial Serial;
uint32_t millis() { return 600000; }

static DFRobot_GDL screen;
DFRobot_GDL& display() { return screen; }
// The preview's "panel" takes RGB565, 2 bytes per pixel (high byte first).
uint8_t displayBytesPerPixel() { return 2; }
void displayPixelBytes(uint16_t color, uint8_t* out) {
  out[0] = (uint8_t)(color >> 8);
  out[1] = (uint8_t)color;
}
void displayPushPixels(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* pixels) {
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = 0; i < w; i++, pixels += 2) screen.drawPixel(x + i, y + j, (uint16_t)((pixels[0] << 8) | pixels[1]));
}

static Settings currentSettings;
const Settings& settings() { return currentSettings; }
Settings& settingsForEdit() { return currentSettings; }

static SymbolState stocks[cfg::NUM_TICKERS], indexes[cfg::NUM_INDEXES], bench;
SymbolState& stock(int i) { return stocks[i]; }
SymbolState& indexEtf(int i) { return indexes[i]; }
SymbolState& symbolByIndex(int i) { return i < cfg::NUM_TICKERS ? stocks[i] : indexes[i - cfg::NUM_TICKERS]; }
SymbolState& benchmark() { return bench; }
bool isStale(const SymbolState&, uint32_t, bool) { return false; }
const char* watchlistIndexLabel(int k) { return k == 0 ? "S&P 500" : "NASDAQ"; }
const char* quoteFeedProblem() { return nullptr; }

static PriceTarget targets[cfg::NUM_TICKERS][2];
PriceTarget targetGet(int stockIndex, bool above) { return targets[stockIndex][above ? 0 : 1]; }
void targetPut(int stockIndex, bool above, float price) { targets[stockIndex][above ? 0 : 1] = {price, 0}; }

// Settings, watchlist and the ticker check (Settings / watchlist editor screens)
Settings& settingsForEdit();
void settingsSave() {}
const char* cycleSpeedName(uint8_t speed) { return speed == CYCLE_FAST ? "FAST" : (speed == CYCLE_SLOW ? "SLOW" : "NORMAL"); }
const char* watchlistSymbol(int i) { return symbolByIndex(i).symbol; }
bool watchlistIsDefault() { return true; }
int watchlistFind(const char* symbol) {
  for (int i = 0; i < NUM_SYMBOLS; i++)
    if (strcmp(symbolByIndex(i).symbol, symbol) == 0) return i;
  return -1;
}
void watchlistSetStock(int, const char*) {}
uint8_t watchlistIndexChoice(int k) { return (uint8_t)k; }
void watchlistSetIndexChoice(int, uint8_t) {}
void watchlistReset() {}
void storageSaveProfiles() {}
static char lookupSymbol[SYMBOL_MAX_LEN + 1];
void quoteClientLookup(const char* symbol) { snprintf(lookupSymbol, sizeof(lookupSymbol), "%s", symbol); }
bool quoteClientLookupResult(SymbolLookup& out) {   // answers a pending check once, as if Finnhub found it
  if (!lookupSymbol[0]) return false;
  memset(&out, 0, sizeof(out));
  snprintf(out.symbol, sizeof(out.symbol), "%s", lookupSymbol);
  out.status = LookupStatus::Found;
  out.quote.price = 172.50f;
  out.quote.change = 2.10f;
  out.quote.changePct = 1.23f;
  out.profile.valid = true;
  snprintf(out.profile.name, sizeof(out.profile.name), "Broadcom Inc");
  lookupSymbol[0] = '\0';
  return true;
}

// Diagnostics and Wi-Fi setup
PreviewWiFi WiFi;
const char* healthResetReason() { return "power on"; }
void healthMemory(MemoryInfo& m) { m = {148 * 1024, 121 * 1024, 96 * 1024, 7900 * 1024}; }
FeedStatus quoteFeedStatus() { return FeedStatus::Live; }
void quoteClientStats(FeedStats& f) {
  memset(&f, 0, sizeof(f));
  f.lastQuoteOkMs = 590000;
  f.lastTdOkMs = 300000;
  f.finnhubLastMinute = 31;
  f.finnhubTotal = 1234;
  f.tdToday = 42;
  f.historyDue = 1 << 3;   // VRT's history still downloading
}
bool timeLastSync(uint32_t& msAgo) { msAgo = 120000; return true; }
void storageInfo(StorageInfo& s) {
  static const char* const NAMES[STORAGE_FILES] = {"chart", "stats", "names", "hist", "earn", "sect"};
  static const uint32_t SIZES[STORAGE_FILES] = {14200, 640, 2300, 9800, 520, 360};
  s.mounted = true;
  s.usedBytes = 220 * 1024;
  s.totalBytes = 1408 * 1024;
  for (int i = 0; i < STORAGE_FILES; i++) {
    s.names[i] = NAMES[i];
    s.sizes[i] = SIZES[i];
  }
}
const char* wifiSetupPassword() { return "48213907"; }
const char* wifiSetupAddress() { return "192.168.4.1"; }
const char* wifiSetupError() { return ""; }

static SectorQuote sectorQuotes[NUM_SECTOR_QUOTES];
const char* sectorSymbol(int i) { return i < cfg::NUM_SECTORS ? cfg::SECTOR_SYMBOLS[i] : cfg::BENCHMARK_SYMBOL; }
const SectorQuote& sectorQuote(int i) { return sectorQuotes[i]; }
uint32_t sectorsVersion(int) { return 1; }
uint32_t sectorsNewestFetch() { return (uint32_t)time(nullptr) - 60; }

static EtTime NOW = {2026, 11, 12, 13, 46};   // Thu Nov 12 2026, 1:46 PM: market open
bool timeNowET(EtTime& out) { out = NOW; return true; }
bool marketIsOpenNow() { return marketStatus(NOW).phase == MarketPhase::Open; }
WifiState wifiState() { return WifiState::Connected; }
const char* wifiSetupName() { return "MarketTracker-A1B2"; }

// 9:30 and 16:00 Eastern on a date (the PC's TZ is set to Eastern in main()).
static uint32_t etUnix(int y, int m, int d, int hour, int minute) {
  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_isdst = -1;
  return (uint32_t)mktime(&t);
}
void sessionBoundsForDay(int32_t day, uint32_t& openUnix, uint32_t& closeUnix) {
  int y, m, d;
  civilFromDay(day, y, m, d);
  openUnix = etUnix(y, m, d, 9, 30);
  closeUnix = etUnix(y, m, d, 16, 0);
}
void sessionBounds(uint32_t anyTime, uint32_t& openUnix, uint32_t& closeUnix) {
  time_t t = anyTime;
  struct tm et;
  localtime_r(&t, &et);
  sessionBoundsForDay(dayNumber(et.tm_year + 1900, et.tm_mon + 1, et.tm_mday), openUnix, closeUnix);
}

// ---------- sample data ----------
// A random walk from a to b over n points, the same as the design canvas mockups' path()
// (so a chart here looks like the one on the canvas).
static void walk(uint32_t seed, int n, float a, float b, float wiggle, float* out) {
  uint32_t x = (seed * 7919u) % 233280u;
  float v = 0;
  for (int i = 0; i < n; i++) {
    x = (x * 9301u + 49297u) % 233280u;
    v += x / 233280.0f - 0.5f;
    out[i] = v;
  }
  float first = out[0], last = out[n - 1];
  for (int i = 0; i < n; i++) {
    float t = (float)i / (n - 1);
    out[i] = a + (b - a) * t + (out[i] - (first + (last - first) * t)) * wiggle;
  }
}

static void fill(SymbolState& s, const char* sym, const char* name, float last, float chg, uint32_t seed) {
  s.symbol = sym;
  Quote q;
  q.price = last;
  q.change = chg;
  q.prevClose = last - chg;
  q.changePct = chg / q.prevClose * 100.0f;
  q.open = q.prevClose + chg * 0.3f;
  s.applyQuote(q, 0, false, 0);
  uint32_t open = etUnix(NOW.year, NOW.month, NOW.day, 9, 30);
  const int N = 78;             // 9:30 to 3:55, every 5 minutes
  float v[N];
  walk(seed, N, q.open, last, fabsf(chg) * 0.5f + last * 0.002f, v);
  float hi = v[0], lo = v[0];
  for (int i = 0; i < N; i++) {
    s.intraday.push(v[i], open + i * 300);
    if (v[i] > hi) hi = v[i];
    if (v[i] < lo) lo = v[i];
  }
  s.quote.high = hi;
  s.quote.low = lo;
  if (name) {
    s.profile.valid = true;
    snprintf(s.profile.name, sizeof(s.profile.name), "%s", name);
  }
}

static void sampleData() {
  fill(stocks[0], "NVDA", "NVIDIA Corporation", 131.52f, 1.62f, 3);
  fill(stocks[1], "AMD", "Advanced Micro Devices, Inc.", 158.20f, -0.85f, 7);
  fill(stocks[2], "INTC", "Intel Corporation", 22.85f, 0.10f, 11);
  fill(stocks[3], "VRT", "Vertiv Holdings Co", 95.40f, 2.31f, 5);
  fill(stocks[4], "AAPL", "Apple Inc", 228.10f, -0.71f, 13);
  fill(indexes[0], "SPY", nullptr, 582.10f, 2.37f, 21);
  fill(indexes[1], "QQQ", nullptr, 501.20f, -1.10f, 23);
  fill(bench, "SPY", nullptr, 582.10f, 2.37f, 21);

  int32_t today = dayNumber(NOW.year, NOW.month, NOW.day);
  stocks[3].earnings.valid = true;   // VRT reports tomorrow before the open
  stocks[3].earnings.day = today + 1;
  snprintf(stocks[3].earnings.hour, sizeof(stocks[3].earnings.hour), "bmo");
  stocks[0].earnings.valid = true;   // NVDA a week out, after the close
  stocks[0].earnings.day = today + 7;
  snprintf(stocks[0].earnings.hour, sizeof(stocks[0].earnings.hour), "amc");

  Fundamentals& f = stocks[0].fundamentals;   // NVDA: 52W range, P/E 52.4, market cap 3.22T
  f.valid = true;
  f.high52 = 152.89f;
  f.low52 = 86.62f;
  f.epsTTM = 131.52f / 52.4f;
  f.sharesM = 3220000.0f / 131.52f;
  f.fetchedDay = today - 2;   // downloaded 2 days ago
  targets[0][0].price = 132.50f;   // NVDA: alert above 132.50 (armed)

  struct { const char* sym; float pct; } SECT[] = {{"XLK", 1.84f}, {"XLF", 0.62f}, {"XLV", 0.18f}, {"XLY", 0.95f},
                                                  {"XLC", 1.21f}, {"XLI", 0.40f}, {"XLP", -0.48f}, {"XLE", -1.62f},
                                                  {"XLU", -0.91f}, {"XLRE", -0.33f}, {"XLB", -0.05f}};
  for (int i = 0; i < NUM_SECTOR_QUOTES; i++) {
    SectorQuote& q = sectorQuotes[i];
    q.valid = true;
    q.price = 100.0f + i * 7.3f;
    q.changePct = i == BENCHMARK_INDEX ? 0.41f : 0;
    for (auto& s : SECT)
      if (i < cfg::NUM_SECTORS && strcmp(s.sym, cfg::SECTOR_SYMBOLS[i]) == 0) q.changePct = s.pct;
    q.change = q.price * q.changePct / 100.0f;
  }
}

// ---------- output ----------
static void save(const char* dir, const char* name) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
  FILE* f = fopen(path, "wb");
  if (!f) {
    printf("can't write %s\n", path);
    return;
  }
  const int W = DFRobot_GDL::W, H = DFRobot_GDL::H, row = W * 3, size = 54 + row * H;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { for (int k = 0; k < 4; k++) hdr[at + k] = (uint8_t)(v >> (8 * k)); };
  put32(2, size);
  put32(10, 54);
  put32(14, 40);
  put32(18, W);
  put32(22, H);
  hdr[26] = 1;
  hdr[28] = 24;
  fwrite(hdr, 1, 54, f);
  for (int y = H - 1; y >= 0; y--) {   // BMP rows go bottom-up
    for (int x = 0; x < W; x++) {
      uint16_t c = screen.fb[y * W + x];
      uint8_t bgr[3] = {(uint8_t)((c & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                        (uint8_t)((c >> 11) * 255 / 31)};
      fwrite(bgr, 1, 3, f);
    }
  }
  fclose(f);
  printf("saved %s\n", path);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  if (argc > 2) currentSettings.theme = (uint8_t)atoi(argv[2]);
  _putenv("TZ=EST5EDT");   // chart times are Eastern, like the device's TZ setting
  _tzset();
  sampleData();

  splash::drawFull();
  splash::Boot boot;
  boot.wifi = splash::Step::Done;
  boot.clock = splash::Step::Done;
  boot.priced = 5;
  boot.total = NUM_SYMBOLS;
  splash::update(boot);
  save(dir, "01_splash");

  home::drawFull(0);
  save(dir, "02_home");

  detail::drawFull(0, true, detail::Page::Day);
  save(dir, "03_detail_1d");
  detail::setCompare(0, true);
  save(dir, "04_detail_vs_spy");
  detail::setCompare(0, false);
  detail::setPage(0, detail::Page::Stats);
  save(dir, "05_detail_stats");

  // 5D: 4 earlier sessions of backfilled 30-minute bars, plus today's live quotes once a
  // minute with an hour missing (11:00 to 12:00, the tracker was off).
  {
    SymbolState& s = stocks[0];
    History& h = s.history;
    h.valid = true;
    h.barCount = 0;
    int32_t today = dayNumber(NOW.year, NOW.month, NOW.day);
    const int32_t back[] = {6, 3, 2, 1};   // Fri, Mon, Tue, Wed before Thu Nov 12
    float level = 124.0f;
    for (int d = 0; d < 4; d++) {
      uint32_t open, close;
      sessionBoundsForDay(today - back[d], open, close);
      float v[13];
      walk(31 + d, 13, level, level + 1.5f, 1.4f, v);
      for (int k = 0; k < 13; k++) {
        h.barTime[h.barCount] = open + (k + 1) * 1800;
        h.barClose[h.barCount++] = v[k];
      }
      level += 1.4f;
    }
    PriceSeries saved = s.intraday;
    s.intraday.clear();
    uint32_t open, close;
    sessionBoundsForDay(today, open, close);
    float v[256];
    walk(3, 256, 129.9f, 131.52f, 2.0f, v);
    for (int k = 0; k < 256; k++) {
      uint32_t t = open + 60 + k * 60;
      if (t >= open + 5400 && t < open + 9000) continue;   // the hole
      s.intraday.push(v[k], t);
    }
    detail::drawFull(0, true, detail::Page::FiveDay);
    save(dir, "05b_detail_5d");
    s.intraday = saved;
    h.barCount = 0;
  }

  sectorsui::drawFull();
  save(dir, "06_sectors");
  currentSettings.sectorMap = SECTOR_MAP_GRID;
  sectorsui::drawFull();
  save(dir, "06b_sectors_grid");
  currentSettings.sectorMap = SECTOR_MAP_SIZED;

  NOW = {2026, 11, 12, 21, 47};   // Thu 9:47 PM: closed, the overnight clock's time
  clockui::drawFull();
  save(dir, "07_clock");
  NOW = {2026, 11, 12, 13, 46};

  home::drawFull(0);
  AlertEvent target = {0, AlertDirection::Up, true, 132.50f};
  banner::draw(target);
  save(dir, "08_banner_target");
  AlertEvent move = {3, AlertDirection::Up, false, 0};
  banner::draw(move);
  save(dir, "08b_banner_up");
  AlertEvent drop = {1, AlertDirection::Down, false, 0};
  banner::draw(drop);
  save(dir, "08c_banner_down");

  // Settings: page 1 with THEME highlighted, page 2
  settingsui::drawFull(5);
  save(dir, "09_settings_p1");
  settingsui::drawFull(8);
  save(dir, "09b_settings_p2");

  // Watchlist editor, driven by taps like a finger would
  auto tap = [](int16_t x, int16_t y) {
    InputEvent ev = {InputType::Tap, x, y};
    watchui::handle(ev, 0);
  };
  auto keyTap = [&](int k) { tap(7 + (k % 10) * 47 + 21, 92 + (k / 10) * 50 + 22); };
  watchui::open();
  save(dir, "10_watchlist");
  tap(240, 30 + 10);        // NVDA's row -> its slot screen
  save(dir, "10b_watchlist_slot");
  tap(240, 30 + 31 + 10);   // Alert above -> keypad (shows the armed 132.50)
  save(dir, "10c_keypad");
  InputEvent back = {InputType::Back, 0, 0};
  watchui::handle(back, 0);   // back to the slot screen
  tap(240, 30 + 10);          // Change ticker -> keyboard
  for (char c : {'A', 'V', 'G', 'O'}) keyTap(c - 'A');
  save(dir, "10d_keyboard");
  tap(8 + 2 * 158 + 74, 250 + 22);   // OK -> checking
  save(dir, "10e_checking");
  watchui::update(0);                // the answer arrives -> confirm
  save(dir, "10f_confirm");

  // Diagnostics: the three tabs
  diagui::open(600000);
  save(dir, "11_diag_device");
  InputEvent next = {InputType::Next, 0, 0};
  diagui::handle(next, 600000);
  save(dir, "11b_diag_network");
  diagui::handle(next, 600000);
  save(dir, "11c_diag_data");

  wifisetup::drawFull();
  save(dir, "12_wifi_setup");
  return 0;
}
