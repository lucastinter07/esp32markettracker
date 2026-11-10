#include "app/screen_manager.h"
#include <Arduino.h>
#include "app/input_event.h"
#include "config.h"
#include "data/market_data.h"
#include "hal/buzzer.h"
#include "hal/display.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "ui/detail_screen.h"
#include "ui/home_screen.h"
#include "ui/settings_screen.h"
#include "ui/splash_screen.h"
#include "ui/watchlist_screen.h"
#include "ui/setup_screen.h"

static const uint32_t SETTINGS_IDLE_MS = 60000;   // Settings returns to Home after 60 s untouched

static int wrap(int i) {
  return (i + cfg::NUM_TICKERS) % cfg::NUM_TICKERS;
}

void ScreenManager::begin(uint32_t nowMs) {
  mode_ = Mode::Splash;
  modeStartMs_ = nowMs;
  splash::drawFull();
  Serial.println("screen: splash");
}

// Shows what startup is doing and leaves once every symbol has a price, Wi-Fi setup is
// needed, or the feed reports a problem (after SPLASH_MIN_MS), or at SPLASH_MAX_MS regardless.
void ScreenManager::updateSplash(uint32_t nowMs) {
  uint32_t elapsed = nowMs - modeStartMs_;
  int priced = 0;
  for (int i = 0; i < NUM_SYMBOLS; i++) {
    if (symbolByIndex(i).hasData) priced++;
  }
  WifiState wifi = wifiState();
  EtTime now;
  bool problem = quoteFeedProblem() != nullptr && quoteFeedStatus() != FeedStatus::Starting;

  char status[40];
  int percent;
  if (wifi == WifiState::Setup) {
    snprintf(status, sizeof(status), "Wi-Fi setup needed");
    percent = 100;
  } else if (priced == 0 && wifi == WifiState::Connecting) {
    snprintf(status, sizeof(status), "Connecting to Wi-Fi...");
    percent = min<uint32_t>(elapsed * 25 / cfg::SPLASH_MAX_MS, 25);
  } else if (priced == 0 && !timeNowET(now)) {
    snprintf(status, sizeof(status), "Syncing clock...");
    percent = 30;
  } else {
    snprintf(status, sizeof(status), "Loading prices %d/%d", priced, NUM_SYMBOLS);
    percent = 35 + 65 * priced / NUM_SYMBOLS;
  }
  splash::update(status, percent);

  bool done = priced == NUM_SYMBOLS || wifi == WifiState::Setup || problem;
  if ((done && elapsed >= cfg::SPLASH_MIN_MS) || elapsed >= cfg::SPLASH_MAX_MS) finishSplash(nowMs);
}

void ScreenManager::finishSplash(uint32_t nowMs) {
  if (wifiState() == WifiState::Setup) showSetup();
  else showHome(nowMs);
}

void ScreenManager::showSetup() {
  mode_ = Mode::Setup;
  wifisetup::drawFull();
  Serial.println("screen: wifi setup");
}

void ScreenManager::showSettings(uint32_t nowMs, int row) {
  mode_ = Mode::Settings;
  modeStartMs_ = nowMs;
  settingsRow_ = row;
  settingsui::drawFull(settingsRow_);
  Serial.println("screen: settings");
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
  detail::drawFull(ticker, pinned, page_);
  Serial.printf("screen: detail %d (%s)\n", ticker, pinned ? "pinned" : "auto");
}

void ScreenManager::update(uint32_t nowMs) {
  InputEvent ev;
  while (inputGet(ev)) handleInput(ev, nowMs);

  if (mode_ == Mode::Splash) {
    updateSplash(nowMs);   // the Wi-Fi setup takeover below waits until the splash is done
    return;
  }

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
        page_ = detail::Page::Day;   // the auto cycle always starts on the day chart
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
        // Halfway through, turn to the stats page.
        if (page_ != detail::Page::Stats && elapsed >= cfg::DETAIL_DURATION_MS / 2) {
          page_ = detail::Page::Stats;
          detail::setPage(ticker_, page_);
        }
        detail::update(ticker_);
        detail::drawCountdown(1.0f - (float)elapsed / cfg::DETAIL_DURATION_MS);
      }
      break;

    case Mode::PinnedDetail:
      detail::update(ticker_);   // no timeout: stays until BACK
      break;

    case Mode::Setup:
      break;   // static instructions; the Wi-Fi manager runs the hotspot

    case Mode::Splash:
      break;   // handled above

    case Mode::Settings:
      if (elapsed >= SETTINGS_IDLE_MS) showHome(nowMs);
      else settingsui::update(settingsRow_);
      break;

    case Mode::Watchlist:
      if (watchui::busy()) modeStartMs_ = nowMs;   // waiting on a ticker check is not idle
      if (elapsed >= SETTINGS_IDLE_MS) showHome(nowMs);
      else watchui::update(nowMs);
      break;
  }
}

void ScreenManager::handleInput(const InputEvent& ev, uint32_t nowMs) {
  buzzerClick();
  handleInputAction(ev, nowMs);
  // Drawn after the action so a screen change doesn't wipe it straight away.
  if (cfg::TOUCH_DEBUG && ev.type == InputType::Tap) drawTouchMarker(ev.x, ev.y);
}

void ScreenManager::handleInputAction(const InputEvent& ev, uint32_t nowMs) {

  if (mode_ == Mode::Splash) {
    finishSplash(nowMs);   // any input skips the startup screen
    return;
  }

  if (mode_ == Mode::Setup) {
    if (ev.type == InputType::Select) {
      wifiRetry(nowMs);   // the screen switches to Home once the state leaves Setup
    } else if (ev.type == InputType::Back) {
      setupDismissed_ = true;   // the hotspot keeps running in the background
      showHome(nowMs);
    }
    return;
  }

  // Holding BACK opens Settings from any other screen.
  if (ev.type == InputType::LongBack) {
    if (mode_ != Mode::Settings) showSettings(nowMs);
    return;
  }

  if (mode_ == Mode::Settings) {
    handleSettingsInput(ev, nowMs);
    return;
  }

  if (mode_ == Mode::Watchlist) {
    modeStartMs_ = nowMs;
    if (watchui::handle(ev, nowMs) == watchui::Result::Exit) showSettings(nowMs, settingsui::watchlistRow());
    return;
  }

  if (mode_ == Mode::Home) {
    modeStartMs_ = nowMs;   // any input restarts the 30 s timer
    switch (ev.type) {
      case InputType::Prev:   highlight_ = wrap(highlight_ - 1); break;
      case InputType::Next:   highlight_ = wrap(highlight_ + 1); break;
      case InputType::Select:
        page_ = detail::Page::Day;
        showDetail(highlight_, true, nowMs);
        break;
      case InputType::Tap: {
        int row = home::rowAt(ev.x, ev.y);
        if (cfg::TOUCH_DEBUG) {
          if (row >= 0) Serial.printf("tap %d,%d -> row %d (%s)\n", ev.x, ev.y, row, stock(row).symbol);
          else Serial.printf("tap %d,%d -> nothing (header or index bar)\n", ev.x, ev.y);
        }
        if (home::isMenuTap(ev.x, ev.y)) {
          showSettings(nowMs);
        } else if (row >= 0) {
          highlight_ = row;
          page_ = detail::Page::Day;
          showDetail(row, true, nowMs);
        }
        break;
      }
      case InputType::Back:
      case InputType::LongBack: break;
    }
    return;
  }

  // On a Detail screen (auto or pinned)
  bool backTap = ev.type == InputType::Tap && detail::isBackTap(ev.x, ev.y);
  int tab = ev.type == InputType::Tap ? detail::tabAt(ev.x, ev.y) : -1;
  if (cfg::TOUCH_DEBUG && ev.type == InputType::Tap) {
    Serial.printf("tap %d,%d -> %s\n", ev.x, ev.y, backTap ? "back" : (tab >= 0 ? "tab" : "detail area"));
  }
  if (ev.type == InputType::Back || backTap) {
    showHome(nowMs);
  } else if (ev.type == InputType::Prev || ev.type == InputType::Next) {
    int step = ev.type == InputType::Next ? 1 : -1;
    highlight_ = wrap(ticker_ + step);
    showDetail(highlight_, true, nowMs);   // keeps the current page
  } else if (tab >= 0 || ev.type == InputType::Select) {
    // A tab picks its page; SELECT steps to the next one (1H > 1D > 5D > 1M > STATS > 1H).
    if (tab >= 0) page_ = (detail::Page)tab;
    else page_ = (detail::Page)(((int)page_ + 1) % detail::PAGE_COUNT);
    detail::setPage(ticker_, page_);
    pinIfAuto();
  } else {
    pinIfAuto();   // any other touch while auto-showing: the user is looking, so pin it
  }
}

void ScreenManager::handleSettingsInput(const InputEvent& ev, uint32_t nowMs) {
  modeStartMs_ = nowMs;   // any input restarts the idle timer
  int count = settingsui::rowCount();
  int activateRow = -1;
  switch (ev.type) {
    case InputType::Prev:   settingsRow_ = (settingsRow_ + count - 1) % count; break;
    case InputType::Next:   settingsRow_ = (settingsRow_ + 1) % count; break;
    case InputType::Select: activateRow = settingsRow_; break;
    case InputType::Back:   showHome(nowMs); return;
    case InputType::LongBack: break;
    case InputType::Tap:
      if (settingsui::isBackTap(ev.x, ev.y)) {
        showHome(nowMs);
        return;
      }
      activateRow = settingsui::rowAt(ev.x, ev.y);
      if (activateRow >= 0 && activateRow != settingsRow_) {
        settingsRow_ = activateRow;
        settingsui::update(settingsRow_);   // move the highlight first (cancels a pending confirm)
      }
      break;
  }
  settingsui::Action action = activateRow >= 0 ? settingsui::activate(activateRow, nowMs) : settingsui::Action::None;
  if (action == settingsui::Action::StartWifiSetup) {
    wifiOpenSetup(nowMs);   // update() switches to the setup screen once the hotspot is up
    return;
  }
  if (action == settingsui::Action::OpenWatchlist) {
    mode_ = Mode::Watchlist;
    modeStartMs_ = nowMs;
    watchui::open();
    Serial.println("screen: watchlist");
    return;
  }
  settingsui::update(settingsRow_);
}

void ScreenManager::pinIfAuto() {
  if (mode_ != Mode::AutoDetail) return;
  mode_ = Mode::PinnedDetail;
  detail::setPinned(true);
}
