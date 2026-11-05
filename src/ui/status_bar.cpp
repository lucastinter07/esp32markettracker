#include "ui/status_bar.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "ui/theme.h"

static const char* const DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Everything the bar shows. Compared with what's on screen to decide whether to redraw.
struct BarContent {
  char phase[12];
  uint16_t phaseColor;
  char detail[40];
  char date[12];
  char time[10];
};

static BarContent drawn;
static bool hasDrawn = false;

static bool sameContent(const BarContent& a, const BarContent& b) {
  return a.phaseColor == b.phaseColor && strcmp(a.phase, b.phase) == 0 &&
         strcmp(a.detail, b.detail) == 0 && strcmp(a.date, b.date) == 0 &&
         strcmp(a.time, b.time) == 0;
}

// "45m" or "2h 14m"
static void formatDuration(char* buf, size_t size, int32_t minutes) {
  if (minutes < 60) snprintf(buf, size, "%ldm", (long)minutes);
  else snprintf(buf, size, "%ldh %ldm", (long)(minutes / 60), (long)(minutes % 60));
}

static void buildContent(BarContent& c) {
  memset(&c, 0, sizeof(c));
  WifiState wifi = wifiState();
  EtTime now;

  if (!timeNowET(now)) {
    // No clock yet: show what the network is doing instead.
    snprintf(c.time, sizeof(c.time), "--:--");
    c.phaseColor = theme::ACCENT;
    if (wifi == WifiState::Setup) {
      snprintf(c.phase, sizeof(c.phase), "NO WIFI");
      snprintf(c.detail, sizeof(c.detail), "join %s", wifiSetupName());
    } else if (wifi == WifiState::Connecting) {
      snprintf(c.phase, sizeof(c.phase), "CONNECTING");
    } else {
      snprintf(c.phase, sizeof(c.phase), "SYNCING");
      snprintf(c.detail, sizeof(c.detail), "getting time");
    }
    return;
  }

  MarketStatus s = marketStatus(now);
  char dur[16];
  switch (s.phase) {
    case MarketPhase::Open:
      snprintf(c.phase, sizeof(c.phase), "OPEN");
      c.phaseColor = theme::UP;
      formatDuration(dur, sizeof(dur), s.minutesToClose);
      snprintf(c.detail, sizeof(c.detail), "%s in %s", s.earlyClose ? "early close" : "closes", dur);
      break;
    case MarketPhase::PreMarket:
      snprintf(c.phase, sizeof(c.phase), "PRE-MKT");
      c.phaseColor = theme::ACCENT;
      break;
    case MarketPhase::AfterHours:
      snprintf(c.phase, sizeof(c.phase), "AFTER-HRS");
      c.phaseColor = theme::ACCENT;
      break;
    case MarketPhase::Closed:
      snprintf(c.phase, sizeof(c.phase), "CLOSED");
      c.phaseColor = theme::DIM;
      break;
  }
  if (s.phase != MarketPhase::Open) {
    const char* prefix = s.holiday ? "holiday, " : "";
    if (s.nextOpenIsToday) {
      formatDuration(dur, sizeof(dur), s.minutesToOpen);
      snprintf(c.detail, sizeof(c.detail), "%sopens in %s", prefix, dur);
    } else {
      snprintf(c.detail, sizeof(c.detail), "%sopens %s 9:30", prefix, DAY_NAMES[s.nextOpenWeekday]);
    }
  }
  if (wifi != WifiState::Connected) {
    strncat(c.detail, " (offline)", sizeof(c.detail) - strlen(c.detail) - 1);
  }

  int hour12 = now.hour % 12 == 0 ? 12 : now.hour % 12;
  snprintf(c.time, sizeof(c.time), "%d:%02d %s", hour12, now.minute, now.hour < 12 ? "AM" : "PM");
  snprintf(c.date, sizeof(c.date), "%s %s %d", DAY_NAMES[weekdayOf(now.year, now.month, now.day)],
           MONTH_NAMES[now.month - 1], now.day);
}

void statusBarDraw(bool force) {
  BarContent c;
  buildContent(c);
  if (!force && hasDrawn && sameContent(c, drawn)) return;

  DFRobot_GDL& gfx = display();
  gfx.fillRect(0, 0, cfg::SCREEN_W, theme::HEADER_H, theme::PANEL);

  // Left: phase in large text, detail in small text right after it
  gfx.setTextSize(2);
  gfx.setTextColor(c.phaseColor, theme::PANEL);
  gfx.setCursor(10, 7);
  gfx.print(c.phase);
  int16_t detailX = 10 + strlen(c.phase) * 12 + 8;
  gfx.setTextSize(1);
  gfx.setTextColor(theme::DIM, theme::PANEL);
  gfx.setCursor(detailX, 11);
  gfx.print(c.detail);

  // Right: time in large text against the right edge, date in small text before it
  int16_t timeX = cfg::SCREEN_W - 10 - strlen(c.time) * 12;
  gfx.setTextSize(2);
  gfx.setTextColor(theme::TEXT, theme::PANEL);
  gfx.setCursor(timeX, 7);
  gfx.print(c.time);
  gfx.setTextSize(1);
  gfx.setTextColor(theme::DIM, theme::PANEL);
  gfx.setCursor(timeX - 8 - strlen(c.date) * 6, 11);
  gfx.print(c.date);

  drawn = c;
  hasDrawn = true;
}
