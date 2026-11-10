#include "ui/settings_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/settings.h"
#include "hal/backlight.h"
#include "hal/display.h"
#include "ui/theme.h"

namespace settingsui {

static const int16_t HEADER_H = 40;
static const int16_t BACK_W = 120;
static const int16_t ROW_Y0 = 48, ROW_H = 32;   // 8 rows: 48 .. 304

enum Row { WATCHLIST, CLICKS, CHIME, ALERTS, NIGHT_DIM, NIGHT_HOURS, BRIGHTNESS, WIFI, ROW_COUNT };
static const char* const LABELS[ROW_COUNT] = {
    "Watchlist", "Click sounds", "Open/close chime", "Price alerts", "Night dimming",
    "Night hours",  "Brightness",       "Wi-Fi setup",
};

// The options each stepping row cycles through
static const uint8_t ALERT_STEPS[] = {0, 2, 3, 5, 10};
static const uint8_t BRIGHTNESS_STEPS[] = {100, 80, 60, 40, 20};
static const uint8_t NIGHT_PRESETS[][2] = {{22, 7}, {23, 7}, {21, 6}, {0, 6}};   // start, end hour

static bool wifiConfirm = false;    // first tap on Wi-Fi setup asks for a second tap

static char drawnValue[ROW_COUNT][24];
static int drawnHighlight = -1;

// Index of `value` in `steps`, or -1 if it's not one of them.
static int indexOf(const uint8_t* steps, int count, uint8_t value) {
  for (int i = 0; i < count; i++) {
    if (steps[i] == value) return i;
  }
  return -1;
}

static void valueText(int row, char* buf, size_t size) {
  const Settings& st = settings();
  switch (row) {
    case WATCHLIST:   snprintf(buf, size, "EDIT >"); break;
    case CLICKS:      snprintf(buf, size, st.clickSounds ? "ON" : "OFF"); break;
    case CHIME:       snprintf(buf, size, st.chime ? "ON" : "OFF"); break;
    case ALERTS:
      if (st.alertPercent == 0) snprintf(buf, size, "OFF");
      else snprintf(buf, size, "+/-%d%%", st.alertPercent);
      break;
    case NIGHT_DIM:   snprintf(buf, size, st.nightDim ? "ON" : "OFF"); break;
    case NIGHT_HOURS: snprintf(buf, size, "%02d:00-%02d:00", st.nightStart, st.nightEnd); break;
    case BRIGHTNESS:  snprintf(buf, size, "%d%%", st.brightness); break;
    case WIFI:        snprintf(buf, size, wifiConfirm ? "Tap again" : "Start"); break;
    default:          buf[0] = '\0';
  }
}

static void drawRow(int row, bool highlighted) {
  DFRobot_GDL& gfx = display();
  int16_t y = ROW_Y0 + row * ROW_H;
  uint16_t bg = highlighted ? theme::HIGHLIGHT : theme::BG;
  gfx.fillRect(0, y, cfg::SCREEN_W, ROW_H - 2, bg);
  gfx.setTextSize(2);
  int16_t ty = y + (ROW_H - 2 - 16) / 2;
  gfx.setTextColor(theme::TEXT, bg);
  gfx.setCursor(16, ty);
  gfx.print(LABELS[row]);
  char value[24];
  valueText(row, value, sizeof(value));
  gfx.setTextColor(theme::ACCENT, bg);
  gfx.setCursor(cfg::SCREEN_W - 16 - (int16_t)strlen(value) * 12, ty);   // right-aligned
  gfx.print(value);
  strcpy(drawnValue[row], value);
}

int watchlistRow() {
  return WATCHLIST;
}

int rowCount() {
  return ROW_COUNT;
}

void drawFull(int highlight) {
  DFRobot_GDL& gfx = display();
  gfx.fillScreen(theme::BG);
  gfx.fillRect(0, 0, cfg::SCREEN_W, HEADER_H, theme::PANEL);
  gfx.fillRect(0, 0, BACK_W, HEADER_H, theme::HIGHLIGHT);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::TEXT, theme::HIGHLIGHT);
  gfx.setCursor(12, 12);
  gfx.print("< BACK");
  gfx.setTextSize(3);
  gfx.setTextColor(theme::TEXT, theme::PANEL);
  gfx.setCursor(BACK_W + 20, 9);
  gfx.print("SETTINGS");
  wifiConfirm = false;
  for (int i = 0; i < ROW_COUNT; i++) drawRow(i, i == highlight);
  drawnHighlight = highlight;
}

void update(int highlight) {
  if (highlight != drawnHighlight) wifiConfirm = false;   // moving away cancels the confirm
  for (int i = 0; i < ROW_COUNT; i++) {
    char value[24];
    valueText(i, value, sizeof(value));
    bool highlightChanged = (i == highlight) != (i == drawnHighlight);
    if (highlightChanged || strcmp(value, drawnValue[i]) != 0) drawRow(i, i == highlight);
  }
  drawnHighlight = highlight;
}

Action activate(int row, uint32_t nowMs) {
  (void)nowMs;
  Settings& st = settingsForEdit();
  int i;
  switch (row) {
    case WATCHLIST: return Action::OpenWatchlist;
    case CLICKS:    st.clickSounds = !st.clickSounds; break;
    case CHIME:     st.chime = !st.chime; break;
    case NIGHT_DIM: st.nightDim = !st.nightDim; break;
    case ALERTS:
      i = indexOf(ALERT_STEPS, sizeof(ALERT_STEPS), st.alertPercent);
      st.alertPercent = ALERT_STEPS[(i + 1) % sizeof(ALERT_STEPS)];
      break;
    case BRIGHTNESS:
      i = indexOf(BRIGHTNESS_STEPS, sizeof(BRIGHTNESS_STEPS), st.brightness);
      st.brightness = BRIGHTNESS_STEPS[(i + 1) % sizeof(BRIGHTNESS_STEPS)];
      backlightSet(st.brightness);   // takes effect immediately
      break;
    case NIGHT_HOURS: {
      int count = sizeof(NIGHT_PRESETS) / sizeof(NIGHT_PRESETS[0]);
      int current = -1;
      for (int k = 0; k < count; k++) {
        if (NIGHT_PRESETS[k][0] == st.nightStart && NIGHT_PRESETS[k][1] == st.nightEnd) current = k;
      }
      int next = (current + 1) % count;
      st.nightStart = NIGHT_PRESETS[next][0];
      st.nightEnd = NIGHT_PRESETS[next][1];
      break;
    }
    case WIFI:
      if (!wifiConfirm) {
        wifiConfirm = true;   // ask for a second tap
        return Action::None;
      }
      wifiConfirm = false;
      return Action::StartWifiSetup;
    default:
      return Action::None;
  }
  settingsSave();
  return Action::None;
}

int rowAt(int16_t x, int16_t y) {
  (void)x;
  if (y < ROW_Y0) return -1;
  int row = (y - ROW_Y0) / ROW_H;
  return row < ROW_COUNT ? row : -1;
}

bool isBackTap(int16_t x, int16_t y) {
  return x < BACK_W + 40 && y < HEADER_H + 4;
}

}  // namespace settingsui
