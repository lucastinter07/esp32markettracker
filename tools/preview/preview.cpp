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
#include "config.h"
#include "data/market_data.h"
#include "data/settings.h"
#include "data/watchlist.h"
#include "hal/display.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "ui/home_screen.h"
#include "ui/splash_screen.h"

// ---------- stand-ins for the firmware modules the screens read ----------
PreviewSerial Serial;
uint32_t millis() { return 600000; }

static DFRobot_GDL screen;
DFRobot_GDL& display() { return screen; }
void displayPushPixels(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* rgb666) {
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = 0; i < w; i++, rgb666 += 3) {
      uint16_t c = (uint16_t)(((rgb666[0] >> 3) << 11) | ((rgb666[1] >> 2) << 5) | (rgb666[2] >> 3));
      screen.drawPixel(x + i, y + j, c);
    }
}

static Settings currentSettings;
const Settings& settings() { return currentSettings; }

static SymbolState stocks[cfg::NUM_TICKERS], indexes[cfg::NUM_INDEXES], bench;
SymbolState& stock(int i) { return stocks[i]; }
SymbolState& indexEtf(int i) { return indexes[i]; }
SymbolState& symbolByIndex(int i) { return i < cfg::NUM_TICKERS ? stocks[i] : indexes[i - cfg::NUM_TICKERS]; }
SymbolState& benchmark() { return bench; }
bool isStale(const SymbolState&, uint32_t, bool) { return false; }
const char* watchlistIndexLabel(int k) { return k == 0 ? "S&P 500" : "NASDAQ"; }
const char* quoteFeedProblem() { return nullptr; }

static EtTime NOW = {2026, 11, 12, 13, 46};   // Thu Nov 12 2026, 1:46 PM: market open
bool timeNowET(EtTime& out) { out = NOW; return true; }
bool marketIsOpenNow() { return marketStatus(NOW).phase == MarketPhase::Open; }
WifiState wifiState() { return WifiState::Connected; }
const char* wifiSetupName() { return "MarketTracker-A1B2"; }

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
  q.open = q.prevClose;
  q.high = last + fabsf(chg) * 0.4f;
  q.low = q.prevClose - fabsf(chg) * 0.3f;
  s.applyQuote(q, 0, false, 0);
  uint32_t open = 1794493800;   // 9:30 ET on Nov 12 2026 (unix seconds)
  const int N = 78;             // 9:30 to 3:55, every 5 minutes
  float v[N];
  walk(seed, N, q.prevClose, last, fabsf(chg) * 0.5f + last * 0.002f, v);
  for (int i = 0; i < N; i++) s.intraday.push(v[i], open + i * 300);
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
  int32_t today = dayNumber(NOW.year, NOW.month, NOW.day);
  stocks[3].earnings.valid = true;   // VRT reports in 3 days
  stocks[3].earnings.day = today + 3;
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
  return 0;
}
