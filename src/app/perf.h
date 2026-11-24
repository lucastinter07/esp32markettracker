// Performance measurements (Phase 10A): boot milestones, how long each screen takes to
// draw, main-loop time, and an optional SPI speed test at boot. Everything goes to the
// serial log as "perf:" lines: each milestone as it happens, a boot summary once the first
// prices are in, and a summary every cfg::PERF_SUMMARY_S (hourly, next to the health line).
#pragma once
#include <stdint.h>

// Boot milestones, in ms since power-on. Each is recorded the first time only.
enum class PerfMark : uint8_t {
  SetupDone,      // setup() finished
  WifiUp,         // Wi-Fi connected
  ClockSynced,    // SNTP time known
  FirstPrice,     // the first live quote reached the screens
  AllPriced,      // every stock and index slot has a live quote
  SpyPriced,      // SPY (compare charts) has a live quote
  BootPassDone,   // quote task: first quote pass done, downloads may start
  SplashDone,     // the splash screen handed over
  DownloadsDone,  // quote task: nothing left to download (names, stats, history, ...)
  Count
};
void perfMark(PerfMark m);           // safe from either core
void perfSymbolPriced(int index);    // main loop: a live quote for symbol `index` was applied

// What was drawn. Full = a whole screen (on a screen switch); the others are partial
// redraws, counted only when they took at least 1 ms (i.e. something was actually drawn).
enum class PerfScreen : uint8_t {
  Splash, Home, Detail, DetailPage, Sectors, Clock, Settings, Watchlist, Diagnostics, Setup, Banner,
  Input,   // handling one tap / button press, including whatever it redraws
  Count
};

// Times the enclosing block: { PerfDraw t(PerfScreen::Home, true); home::drawFull(...); }
class PerfDraw {
 public:
  PerfDraw(PerfScreen screen, bool full);
  ~PerfDraw();

 private:
  uint32_t startUs_;
  PerfScreen screen_;
  bool full_;
};

void perfLoopTime(uint32_t us);     // main loop: how long one pass took (without its delay)
void perfUpdate(uint32_t nowMs);    // main loop: Wi-Fi / clock milestones and the summaries
void perfSpiBench();                // cfg::PERF_SPI_BENCH: fill / text / line timings, logged
