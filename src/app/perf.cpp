#include "app/perf.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "data/market_data.h"   // NUM_SYMBOLS
#include "hal/display.h"
#include "net/time_sync.h"
#include "ui/gfx.h"

// ---------- boot milestones ----------

static volatile uint32_t marks[(int)PerfMark::Count];   // ms since boot, 0 = not yet
static bool printed[(int)PerfMark::Count];
static bool summaryPrinted = false;
static uint32_t pricedMask = 0;   // bit i = symbol i had a live quote (main loop only)

static const char* markName(PerfMark m) {
  switch (m) {
    case PerfMark::SetupDone:    return "setup done";
    case PerfMark::WifiUp:       return "wi-fi up";
    case PerfMark::ClockSynced:  return "clock synced";
    case PerfMark::FirstPrice:   return "first price";
    case PerfMark::AllPriced:    return "all symbols priced";
    case PerfMark::SpyPriced:    return "SPY priced";
    case PerfMark::BootPassDone: return "quote pass done, downloads start";
    case PerfMark::SplashDone:   return "splash done";
    case PerfMark::Count:        break;
  }
  return "?";
}

void perfMark(PerfMark m) {
  uint32_t now = millis();
  if (marks[(int)m] == 0) marks[(int)m] = now ? now : 1;
}

void perfSymbolPriced(int index) {
  if (index < 0 || index >= NUM_SYMBOLS) return;
  perfMark(PerfMark::FirstPrice);
  pricedMask |= 1u << index;
  if (pricedMask == (1u << NUM_SYMBOLS) - 1) perfMark(PerfMark::AllPriced);
}

// ---------- screen draw times ----------

struct DrawStats {
  uint32_t count;
  uint64_t totalUs;
  uint32_t maxUs;
};
static DrawStats fullStats[(int)PerfScreen::Count];   // since the last summary
static DrawStats partStats[(int)PerfScreen::Count];

static const char* screenName(PerfScreen s) {
  switch (s) {
    case PerfScreen::Splash:      return "SPLASH";
    case PerfScreen::Home:        return "HOME";
    case PerfScreen::Detail:      return "DETAIL";
    case PerfScreen::DetailPage:  return "DETAIL-PAGE";
    case PerfScreen::Sectors:     return "SECTORS";
    case PerfScreen::Clock:       return "CLOCK";
    case PerfScreen::Settings:    return "SETTINGS";
    case PerfScreen::Watchlist:   return "WATCHLIST";
    case PerfScreen::Diagnostics: return "DIAG";
    case PerfScreen::Setup:       return "WIFI-SETUP";
    case PerfScreen::Banner:      return "BANNER";
    case PerfScreen::Input:       return "INPUT";
    case PerfScreen::Count:       break;
  }
  return "?";
}

PerfDraw::PerfDraw(PerfScreen screen, bool full) : startUs_(micros()), screen_(screen), full_(full) {}

PerfDraw::~PerfDraw() {
  uint32_t us = micros() - startUs_;
  if (!full_ && us < 1000) return;   // nothing changed, nothing drawn
  DrawStats& st = (full_ ? fullStats : partStats)[(int)screen_];
  st.count++;
  st.totalUs += us;
  if (us > st.maxUs) st.maxUs = us;
  if (full_ && cfg::PERF_LOG_DRAWS) Serial.printf("perf: draw %s %lu ms\n", screenName(screen_), (unsigned long)(us / 1000));
}

// ---------- main loop ----------

static uint32_t loopCount = 0;
static uint64_t loopTotalUs = 0;
static uint32_t loopMaxUs = 0;
static uint32_t loopOver50 = 0;
static uint32_t loopOver100 = 0;
static uint32_t loopOver250 = 0;

void perfLoopTime(uint32_t us) {
  loopCount++;
  loopTotalUs += us;
  if (us > loopMaxUs) loopMaxUs = us;
  if (us > 50000) loopOver50++;
  if (us > 100000) loopOver100++;
  if (us > 250000) loopOver250++;
}

// "HOME 12/310/342" = count / average ms / max ms, for every screen that was drawn.
static void printDrawLine(const char* title, DrawStats* stats) {
  char line[400];
  int len = snprintf(line, sizeof(line), "perf: %s (count/avg/max ms):", title);
  bool any = false;
  for (int i = 0; i < (int)PerfScreen::Count && len < (int)sizeof(line) - 40; i++) {
    const DrawStats& st = stats[i];
    if (st.count == 0) continue;
    any = true;
    len += snprintf(line + len, sizeof(line) - len, " %s %lu/%lu/%lu", screenName((PerfScreen)i),
                    (unsigned long)st.count, (unsigned long)(st.totalUs / st.count / 1000),
                    (unsigned long)(st.maxUs / 1000));
  }
  if (!any) snprintf(line + len, sizeof(line) - len, " none");
  Serial.println(line);
}

static void printSummary(uint32_t periodS) {
  float avgMs = loopCount ? (float)loopTotalUs / loopCount / 1000.0f : 0;
  Serial.printf("perf: last %lu min: loop avg %.2f ms, max %lu ms; over 50 ms %lux, over 100 ms %lux, over 250 ms %lux\n",
                (unsigned long)(periodS / 60), avgMs, (unsigned long)(loopMaxUs / 1000), (unsigned long)loopOver50,
                (unsigned long)loopOver100, (unsigned long)loopOver250);
  printDrawLine("full redraws", fullStats);
  printDrawLine("partial redraws", partStats);
  loopCount = 0;
  loopTotalUs = 0;
  loopMaxUs = 0;
  loopOver50 = loopOver100 = loopOver250 = 0;
  memset(fullStats, 0, sizeof(fullStats));
  memset(partStats, 0, sizeof(partStats));
}

static void printBootSummary() {
  char line[300];
  int len = snprintf(line, sizeof(line), "perf: boot summary (s):");
  for (int i = 0; i < (int)PerfMark::Count; i++) {
    uint32_t t = marks[i];
    if (t) len += snprintf(line + len, sizeof(line) - len, " %s %.1f;", markName((PerfMark)i), t / 1000.0f);
    else len += snprintf(line + len, sizeof(line) - len, " %s --;", markName((PerfMark)i));
    if (len >= (int)sizeof(line)) break;
  }
  Serial.println(line);
}

void perfUpdate(uint32_t nowMs) {
  if (!marks[(int)PerfMark::WifiUp] && WiFi.isConnected()) perfMark(PerfMark::WifiUp);
  if (!marks[(int)PerfMark::ClockSynced]) {
    EtTime t;
    if (timeNowET(t)) perfMark(PerfMark::ClockSynced);
  }
  for (int i = 0; i < (int)PerfMark::Count; i++) {
    if (marks[i] && !printed[i]) {
      printed[i] = true;
      Serial.printf("perf: +%.2f s %s\n", marks[i] / 1000.0f, markName((PerfMark)i));
    }
  }

  // Boot summary once the screens have every price (or after 3 minutes regardless).
  if (!summaryPrinted) {
    bool done = marks[(int)PerfMark::AllPriced] && marks[(int)PerfMark::SpyPriced] &&
                marks[(int)PerfMark::SplashDone] && marks[(int)PerfMark::BootPassDone];
    if (done || nowMs > 180000) {
      summaryPrinted = true;
      printBootSummary();
    }
  }

  static uint32_t lastSummaryMs = 0;
  if (nowMs - lastSummaryMs >= cfg::PERF_SUMMARY_S * 1000UL) {
    lastSummaryMs = nowMs;
    printSummary(cfg::PERF_SUMMARY_S);
  }
}

// ---------- SPI speed test ----------

void perfSpiBench() {
  DFRobot_GDL& d = display();
  const uint32_t pixels = (uint32_t)cfg::SCREEN_W * cfg::SCREEN_H;

  uint32_t t0 = micros();
  for (int i = 0; i < 4; i++) d.fillScreen(i & 1 ? 0x18E3 : 0x0000);
  uint32_t fillUs = (micros() - t0) / 4;
  float mbitPerS = pixels * 8.0f * displayBytesPerPixel() / fillUs;

  t0 = micros();
  for (int i = 0; i < 20; i++) d.fillRect(10 + i * 4, 10, 100, 100, i & 1 ? 0x07E0 : 0x001F);
  uint32_t rectUs = (micros() - t0) / 20;

  const char* line40 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.%";
  t0 = micros();
  for (int i = 0; i < 20; i++) ui::text(10, 130 + (i % 4) * 10, line40, 1, 0xFFFF, 0x0000);
  uint32_t text1Us = (micros() - t0) / 20;

  t0 = micros();
  for (int i = 0; i < 20; i++) ui::text(10, 180, "ABCDEFGHIJKLMNOPQRST", 2, 0xFFFF, 0x0000);
  uint32_t text2Us = (micros() - t0) / 20;

  t0 = micros();
  for (int i = 0; i < 20; i++) ui::text(10, 210, "1234.56", 4, 0xFFFF, 0x0000);
  uint32_t text4Us = (micros() - t0) / 20;

  t0 = micros();
  for (int i = 0; i < 20; i++) d.drawLine(250, 120 + i, 470, 300 - i, 0xFFE0);
  uint32_t lineUs = (micros() - t0) / 20;

  t0 = micros();
  for (int i = 0; i < 100; i++) d.drawFastHLine(250, 120 + i, 220, 0xF800);
  uint32_t hlineUs = (micros() - t0) / 100;

  t0 = micros();
  for (int i = 0; i < 100; i++) d.drawPixel(470 - i, 310, 0xFFFF);
  uint32_t pixelUs = (micros() - t0) / 100;

  Serial.printf("perf: SPI bench at %lu MHz: full screen %lu ms (%.1f Mbit/s), rect 100x100 %lu us\n",
                (unsigned long)(cfg::TFT_SPI_HZ / 1000000), (unsigned long)(fillUs / 1000), mbitPerS,
                (unsigned long)rectUs);
  Serial.printf("perf: SPI bench text: 40 chars size 1 %lu us, 20 chars size 2 %lu us, 7 chars size 4 %lu us\n",
                (unsigned long)text1Us, (unsigned long)text2Us, (unsigned long)text4Us);
  Serial.printf("perf: SPI bench lines: diagonal 220x180 %lu us, h-line 220 px %lu us, pixel %lu us\n",
                (unsigned long)lineUs, (unsigned long)hlineUs, (unsigned long)pixelUs);
  d.fillScreen(0x0000);
}
