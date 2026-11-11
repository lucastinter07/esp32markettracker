// Decides which screen is showing. Starts on the splash screen until the first prices are in.
// Runs the Home (30 s) -> Detail (20 s: chart, then stats) auto cycle,
// pins a Detail screen when the user picks a ticker, and turns InputEvents into actions.
// Holding BACK for 2 s (or tapping the menu icon on Home) opens Settings.
#pragma once
#include <stdint.h>
#include "app/input_event.h"
#include "app/price_alerts.h"
#include "ui/detail_screen.h"

class ScreenManager {
 public:
  void begin(uint32_t nowMs);
  void update(uint32_t nowMs);   // call every loop

 private:
  enum class Mode { Splash, Home, AutoDetail, PinnedDetail, Setup, Settings, Watchlist, Diagnostics };

  void handleInput(const InputEvent& ev, uint32_t nowMs);
  void handleInputAction(const InputEvent& ev, uint32_t nowMs);
  void showHome(uint32_t nowMs);
  void showDetail(int ticker, bool pinned, uint32_t nowMs);
  void showSetup();
  void updateSplash(uint32_t nowMs);
  void finishSplash(uint32_t nowMs);
  void showSettings(uint32_t nowMs, int row = 0);
  void handleSettingsInput(const InputEvent& ev, uint32_t nowMs);
  void pinIfAuto();
  void showBanner(int stockIndex, AlertDirection dir, uint32_t nowMs);
  void dismissBanner(uint32_t nowMs);
  void redrawCurrent();

  Mode mode_ = Mode::Splash;
  uint32_t modeStartMs_ = 0;   // when the current screen (or its timer) started
  int ticker_ = 0;             // ticker shown on the Detail screen
  int nextAutoTicker_ = 0;     // ticker the auto cycle shows next
  int highlight_ = 0;          // selected row on Home (moved by PREV/NEXT)
  bool setupDismissed_ = false;  // BACK pressed on the Wi-Fi setup screen
  int settingsRow_ = 0;        // highlighted row on the Settings screen
  bool bannerShown_ = false;   // a price-alert banner covers the top of the screen
  uint32_t bannerUntilMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  int bannerStock_ = 0;
  detail::Page page_ = detail::Page::Day;   // which Detail page is showing
};
