#include "app/screen_manager.h"
#include <Arduino.h>
#include "app/input_event.h"
#include "config.h"
#include "data/market_data.h"
#include "hal/buzzer.h"
#include "hal/display.h"
#include "net/wifi_manager.h"
#include "ui/detail_screen.h"
#include "ui/home_screen.h"
#include "ui/setup_screen.h"

static int wrap(int i) {
  return (i + cfg::NUM_TICKERS) % cfg::NUM_TICKERS;
}

void ScreenManager::begin(uint32_t nowMs) {
  if (wifiState() == WifiState::Setup) showSetup();
  else showHome(nowMs);
}

void ScreenManager::showSetup() {
  mode_ = Mode::Setup;
  wifisetup::drawFull();
  Serial.println("screen: wifi setup");
}

void ScreenManager::showHome(uint32_t nowMs) {
  mode_ = Mode::Home;
  modeStartMs_ = nowMs;
  home::drawFull(highlight_);
  Serial.println("screen: home");
}

void ScreenManager::showDetail(int ticker, bool pinned, uint32_t nowMs) {
  mode_ = pinned ? Mode::PinnedDetail : Mode::AutoDetail;
  modeStartMs_ = nowMs;
  ticker_ = ticker;
  detail::drawFull(ticker, pinned);
  Serial.printf("screen: detail %d (%s)\n", ticker, pinned ? "pinned" : "auto");
}

void ScreenManager::update(uint32_t nowMs) {
  InputEvent ev;
  while (inputGet(ev)) handleInput(ev, nowMs);

  // Wi-Fi setup takes over the screen until it finishes or the user presses BACK.
  bool inSetup = wifiState() == WifiState::Setup;
  if (!inSetup) setupDismissed_ = false;   // so a later setup shows again
  bool wantSetup = inSetup && !setupDismissed_;
  if (wantSetup && mode_ != Mode::Setup) {
    showSetup();
    return;
  }
  if (!wantSetup && mode_ == Mode::Setup) {
    showHome(nowMs);
    return;
  }

  uint32_t elapsed = nowMs - modeStartMs_;
  switch (mode_) {
    case Mode::Home:
      if (elapsed >= cfg::HOME_DURATION_MS) {
        showDetail(nextAutoTicker_, false, nowMs);
        nextAutoTicker_ = wrap(nextAutoTicker_ + 1);
      } else {
        home::update(highlight_);
      }
      break;

    case Mode::AutoDetail:
      if (elapsed >= cfg::DETAIL_DURATION_MS) {
        showHome(nowMs);
      } else {
        detail::update(ticker_);
        detail::drawCountdown(1.0f - (float)elapsed / cfg::DETAIL_DURATION_MS);
      }
      break;

    case Mode::PinnedDetail:
      detail::update(ticker_);   // no timeout: stays until BACK
      break;

    case Mode::Setup:
      break;   // static instructions; the Wi-Fi manager runs the hotspot
  }
}

void ScreenManager::handleInput(const InputEvent& ev, uint32_t nowMs) {
  buzzerClick();
  handleInputAction(ev, nowMs);
  // Drawn after the action so a screen change doesn't wipe it straight away.
  if (cfg::TOUCH_DEBUG && ev.type == InputType::Tap) drawTouchMarker(ev.x, ev.y);
}

void ScreenManager::handleInputAction(const InputEvent& ev, uint32_t nowMs) {

  if (mode_ == Mode::Setup) {
    if (ev.type == InputType::Select) {
      wifiRetry(nowMs);   // the screen switches to Home once the state leaves Setup
    } else if (ev.type == InputType::Back) {
      setupDismissed_ = true;   // the hotspot keeps running in the background
      showHome(nowMs);
    }
    return;
  }

  if (mode_ == Mode::Home) {
    modeStartMs_ = nowMs;   // any input restarts the 30 s timer
    switch (ev.type) {
      case InputType::Prev:   highlight_ = wrap(highlight_ - 1); break;
      case InputType::Next:   highlight_ = wrap(highlight_ + 1); break;
      case InputType::Select: showDetail(highlight_, true, nowMs); break;
      case InputType::Tap: {
        int row = home::rowAt(ev.x, ev.y);
        if (cfg::TOUCH_DEBUG) {
          if (row >= 0) Serial.printf("tap %d,%d -> row %d (%s)\n", ev.x, ev.y, row, stock(row).symbol);
          else Serial.printf("tap %d,%d -> nothing (header or index bar)\n", ev.x, ev.y);
        }
        if (row >= 0) {
          highlight_ = row;
          showDetail(row, true, nowMs);
        }
        break;
      }
      case InputType::Back: break;
    }
    return;
  }

  // On a Detail screen (auto or pinned)
  bool backTap = ev.type == InputType::Tap && detail::isBackTap(ev.x, ev.y);
  if (cfg::TOUCH_DEBUG && ev.type == InputType::Tap) {
    Serial.printf("tap %d,%d -> %s\n", ev.x, ev.y, backTap ? "back" : "detail area");
  }
  if (ev.type == InputType::Back || backTap) {
    showHome(nowMs);
  } else if (ev.type == InputType::Prev || ev.type == InputType::Next) {
    int step = ev.type == InputType::Next ? 1 : -1;
    highlight_ = wrap(ticker_ + step);
    showDetail(highlight_, true, nowMs);
  } else if (mode_ == Mode::AutoDetail) {
    // Any other touch or SELECT while auto-showing: the user is looking, so pin it.
    mode_ = Mode::PinnedDetail;
    detail::setPinned(true);
  }
}
