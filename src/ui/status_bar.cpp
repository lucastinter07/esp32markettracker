#include "ui/status_bar.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "ui/gfx.h"

static const char* const DAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// What the bar shows; compared with what's on screen to decide whether to redraw.
struct BarContent {
  char left[40];
  char right[20];
};

static BarContent drawn;
static bool hasDrawn = false;

// "45M" or "2H 14M"
static void formatDuration(char* buf, size_t size, int32_t minutes) {
  if (minutes < 60) snprintf(buf, size, "%ldM", (long)minutes);
  else snprintf(buf, size, "%ldH %02ldM", (long)(minutes / 60), (long)(minutes % 60));
}

static void buildContent(BarContent& c) {
  memset(&c, 0, sizeof(c));
  EtTime now;
  if (!timeNowET(now)) {
    // No clock yet: show what the network is doing instead.
    WifiState wifi = wifiState();
    const char* state = wifi == WifiState::Setup ? "NO WI-FI" : (wifi == WifiState::Connecting ? "CONNECTING" : "SYNCING CLOCK");
    snprintf(c.left, sizeof(c.left), G_MENU " %s", state);
    snprintf(c.right, sizeof(c.right), "--:--");
    return;
  }

  // Left: the phase and, after the dot, the time to the next change (or the next open)
  MarketStatus s = marketStatus(now);
  char when[24];
  const char* phase = "CLOSED";
  switch (s.phase) {
    case MarketPhase::Open:       phase = s.earlyClose ? "EARLY CLOSE" : "OPEN"; break;
    case MarketPhase::PreMarket:  phase = "PRE-MARKET"; break;
    case MarketPhase::AfterHours: phase = "AFTER-HRS"; break;
    case MarketPhase::Closed:     phase = s.holiday ? "HOLIDAY" : "CLOSED"; break;
  }
  if (s.phase == MarketPhase::Open) {
    formatDuration(when, sizeof(when), s.minutesToClose);
  } else if (s.nextOpenIsToday) {
    formatDuration(when, sizeof(when), s.minutesToOpen);
  } else {
    snprintf(when, sizeof(when), "%s 9:30", DAY_NAMES[s.nextOpenWeekday]);
  }
  snprintf(c.left, sizeof(c.left), G_MENU " %s " G_DOT " %s", phase, when);

  // Right: "THU 11/12 1:46P"
  int hour12 = now.hour % 12 == 0 ? 12 : now.hour % 12;
  snprintf(c.right, sizeof(c.right), "%s %d/%d %d:%02d%c", DAY_NAMES[weekdayOf(now.year, now.month, now.day)],
           now.month, now.day, hour12, now.minute, now.hour < 12 ? 'A' : 'P');
}

void statusBarDraw(bool force) {
  BarContent c;
  buildContent(c);
  if (!force && hasDrawn && strcmp(c.left, drawn.left) == 0 && strcmp(c.right, drawn.right) == 0) return;

  // If the two sides would meet, the date goes and only the time stays.
  const char* right = c.right;
  if (6 + ui::textWidth(c.left, 2) + 12 > cfg::SCREEN_W - 6 - ui::textWidth(right, 2)) {
    const char* space = strrchr(c.right, ' ');
    if (space) right = space + 1;
  }
  ui::headerBar(c.left, right);
  drawn = c;
  hasDrawn = true;
}
