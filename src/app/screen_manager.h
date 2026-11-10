// Decides which screen is showing. Runs the Home (30 s) -> Detail (20 s: chart, then stats) auto cycle,
// pins a Detail screen when the user picks a ticker, and turns InputEvents into actions.
#pragma once
#include <stdint.h>
#include "app/input_event.h"
#include "ui/detail_screen.h"

class ScreenManager {
 public:
  void begin(uint32_t nowMs);
  void update(uint32_t nowMs);   // call every loop

 private:
  enum class Mode { Home, AutoDetail, PinnedDetail, Setup };

  void handleInput(const InputEvent& ev, uint32_t nowMs);
  void handleInputAction(const InputEvent& ev, uint32_t nowMs);
  void showHome(uint32_t nowMs);
  void showDetail(int ticker, bool pinned, uint32_t nowMs);
  void showSetup();
  void pinIfAuto();

  Mode mode_ = Mode::Home;
  uint32_t modeStartMs_ = 0;   // when the current screen (or its timer) started
  int ticker_ = 0;             // ticker shown on the Detail screen
  int nextAutoTicker_ = 0;     // ticker the auto cycle shows next
  int highlight_ = 0;          // selected row on Home (moved by PREV/NEXT)
  bool setupDismissed_ = false;  // BACK pressed on the Wi-Fi setup screen
  detail::Page page_ = detail::Page::Day;   // which Detail page is showing
};
