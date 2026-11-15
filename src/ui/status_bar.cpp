#include "ui/status_bar.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/time_sync.h"
#include "net/wifi_manager.h"
#include "ui/fonts.h"
#include "ui/gfx.h"
#include "ui/palette.h"

static const char* const DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Everything the line shows. Compared with what's on screen to decide whether to redraw.
struct BarContent {
  char phase[16];
  uint16_t dotColor;
  char detail[40];
  char date[16];
  char time[10];
  const char* notice;
  uint16_t noticeColor;
  uint8_t theme;   // a theme change must repaint too
};

static BarContent drawn;
static bool hasDrawn = false;

static bool sameContent(const BarContent& a, const BarContent& b) {
  return a.dotColor == b.dotColor && a.notice == b.notice && a.noticeColor == b.noticeColor && a.theme == b.theme &&
         strcmp(a.phase, b.phase) == 0 && strcmp(a.detail, b.detail) == 0 && strcmp(a.date, b.date) == 0 &&
         strcmp(a.time, b.time) == 0;
}

// "45m" or "2h 14m"
static void formatDuration(char* buf, size_t size, int32_t minutes) {
  if (minutes < 60) snprintf(buf, size, "%ldm", (long)minutes);
  else snprintf(buf, size, "%ldh %ldm", (long)(minutes / 60), (long)(minutes % 60));
}

static void buildContent(BarContent& c) {
  memset(&c, 0, sizeof(c));
  const ui::Palette& p = ui::pal();
  WifiState wifi = wifiState();
  EtTime now;

  if (!timeNowET(now)) {
    // No clock yet: show what the network is doing instead.
    snprintf(c.time, sizeof(c.time), "--:--");
    c.dotColor = p.target;
    if (wifi == WifiState::Setup) {
      snprintf(c.phase, sizeof(c.phase), "No Wi-Fi");
      snprintf(c.detail, sizeof(c.detail), "join %s", wifiSetupName());
    } else if (wifi == WifiState::Connecting) {
      snprintf(c.phase, sizeof(c.phase), "Connecting");
    } else {
      snprintf(c.phase, sizeof(c.phase), "Syncing");
      snprintf(c.detail, sizeof(c.detail), "getting the time");
    }
    return;
  }

  MarketStatus s = marketStatus(now);
  char dur[16];
  switch (s.phase) {
    case MarketPhase::Open:
      snprintf(c.phase, sizeof(c.phase), "Open");
      c.dotColor = p.up;
      formatDuration(dur, sizeof(dur), s.minutesToClose);
      snprintf(c.detail, sizeof(c.detail), "%s in %s", s.earlyClose ? "early close" : "closes", dur);
      break;
    case MarketPhase::PreMarket:
      snprintf(c.phase, sizeof(c.phase), "Pre-market");
      c.dotColor = p.target;
      break;
    case MarketPhase::AfterHours:
      snprintf(c.phase, sizeof(c.phase), "After hours");
      c.dotColor = p.target;
      break;
    case MarketPhase::Closed:
      snprintf(c.phase, sizeof(c.phase), "Closed");
      c.dotColor = p.sub;
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
  snprintf(c.date, sizeof(c.date), "%s %s %d " UI_DOT, DAY_NAMES[weekdayOf(now.year, now.month, now.day)],
           MONTH_NAMES[now.month - 1], now.day);
}

void statusBarDraw(bool force, const char* notice, uint16_t noticeColor) {
  BarContent c;
  buildContent(c);
  c.notice = notice;
  c.noticeColor = noticeColor;
  c.theme = (uint8_t)(&ui::pal() - &ui::palette(0));
  if (!force && hasDrawn && sameContent(c, drawn)) return;

  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  gfx.fillRect(0, 0, cfg::SCREEN_W, STATUS_H, p.bg);

  // Right: time, with the date just before it
  ui::text(cfg::SCREEN_W - 12, 18, InterSemi12, p.text, c.time, ui::Align::Right);
  int16_t dateRight = cfg::SCREEN_W - 12 - ui::textWidth(InterSemi12, c.time) - 4;
  int16_t dateLeft = c.date[0] ? dateRight - ui::textWidth(Inter12, c.date) : dateRight;
  if (c.date[0]) ui::text(dateRight, 18, Inter12, p.sub, c.date, ui::Align::Right);

  // Left: dot, phase, countdown (cut short if the line is crowded), then the notice pill
  gfx.fillCircle(16, 14, 4, c.dotColor);
  int16_t x = ui::text(26, 18, InterSemi12, p.text, c.phase) + 6;
  int16_t noticeW = notice ? ui::textWidth(InterSemi10, notice) + 16 : 0;
  int16_t room = dateLeft - 10 - x - (notice ? noticeW + 8 : 0);
  char detail[40];
  ui::fitText(Inter12, c.detail, room, detail, sizeof(detail));
  x = ui::text(x, 18, Inter12, p.sub, detail);
  if (notice && x + 8 + noticeW <= dateLeft - 6) {
    ui::pill(x + 8, 5, noticeW, 18, ui::mix(p.bg, noticeColor, 50));
    ui::text(x + 8 + noticeW / 2, 17, InterSemi10, noticeColor, notice, ui::Align::Center);
  }

  drawn = c;
  hasDrawn = true;
}
