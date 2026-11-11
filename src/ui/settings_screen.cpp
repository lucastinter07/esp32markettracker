#include "ui/settings_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/settings.h"
#include "hal/display.h"
#include "ui/theme.h"

namespace settingsui {

static const int16_t HEADER_H = 40;
static const int16_t BACK_W = 120;
static const int16_t ROW_Y0 = 48, ROW_H = 32;   // 7 rows per page: 48 .. 272
static const int ROWS_PER_PAGE = 7;
static const int16_t PAGER_Y = 282, PAGER_H = 36;   // page buttons along the bottom
static const int16_t PAGER_W = 150;

enum Row {
  // page 1
  WATCHLIST, AUTO_CYCLE, CYCLE_SPEED, ALERTS, CLICKS, CHIME, BRIGHTNESS,
  // page 2
  NIGHT_DIM, NIGHT_HOURS, WIFI, DIAGNOSTICS, REFRESH, RESTART,
  ROW_COUNT
};
static const char* const LABELS[ROW_COUNT] = {
    "Watchlist",     "Auto cycle",    "Cycle speed", "Price alerts", "Click sounds",     "Open/close chime",
    "Brightness",    "Night dimming", "Night hours", "Wi-Fi setup",  "Diagnostics",      "Re-download data",
    "Restart device",
};
static const int PAGE_COUNT = (ROW_COUNT + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;

// The options each stepping row cycles through
static const uint8_t ALERT_STEPS[] = {0, 2, 3, 5, 10};
static const uint8_t BRIGHTNESS_STEPS[] = {100, 80, 60, 40, 20};
static const uint8_t NIGHT_PRESETS[][2] = {{22, 7}, {23, 7}, {21, 6}, {0, 6}};   // start, end hour

// Rows that act (Wi-Fi setup, re-download, restart) ask for a second tap first.
static int confirmRow = -1;

static char drawnValue[ROW_COUNT][24];
static int drawnHighlight = -1;
static int drawnPage = -1;

static int pageOf(int row) {
  return row / ROWS_PER_PAGE;
}

// Index of `value` in `steps`, or -1 if it's not one of them.
static int indexOf(const uint8_t* steps, int count, uint8_t value) {
  for (int i = 0; i < count; i++) {
    if (steps[i] == value) return i;
  }
  return -1;
}

static void valueText(int row, char* buf, size_t size) {
  const Settings& st = settings();
  bool confirming = row == confirmRow;
  switch (row) {
    case WATCHLIST:   snprintf(buf, size, "EDIT >"); break;
    case AUTO_CYCLE:  snprintf(buf, size, st.autoCycle ? "ON" : "OFF"); break;
    case CYCLE_SPEED: snprintf(buf, size, "%s", cycleSpeedName(st.cycleSpeed)); break;
    case ALERTS:
      if (st.alertPercent == 0) snprintf(buf, size, "OFF");
      else snprintf(buf, size, "+/-%d%%", st.alertPercent);
      break;
    case CLICKS:      snprintf(buf, size, st.clickSounds ? "ON" : "OFF"); break;
    case CHIME:       snprintf(buf, size, st.chime ? "ON" : "OFF"); break;
    case BRIGHTNESS:  snprintf(buf, size, "%d%%", st.brightness); break;
    case NIGHT_DIM:   snprintf(buf, size, st.nightDim ? "ON" : "OFF"); break;
    case NIGHT_HOURS: snprintf(buf, size, "%02d:00-%02d:00", st.nightStart, st.nightEnd); break;
    case WIFI:        snprintf(buf, size, confirming ? "Tap again" : "Start"); break;
    case DIAGNOSTICS: snprintf(buf, size, "VIEW >"); break;
    case REFRESH:     snprintf(buf, size, confirming ? "Tap again" : "~1 MB"); break;
    case RESTART:     snprintf(buf, size, confirming ? "Tap again" : "Restart"); break;
    default:          buf[0] = '\0';
  }
}

static void drawRow(int row, bool highlighted) {
  DFRobot_GDL& gfx = display();
  int16_t y = ROW_Y0 + (row % ROWS_PER_PAGE) * ROW_H;
  uint16_t bg = highlighted ? theme::HIGHLIGHT : theme::BG;
  gfx.fillRect(0, y, cfg::SCREEN_W, ROW_H - 2, bg);
  gfx.setTextSize(2);
  int16_t ty = y + (ROW_H - 2 - 16) / 2;
  gfx.setTextColor(theme::TEXT, bg);
  gfx.setCursor(16, ty);
  gfx.print(LABELS[row]);
  char value[24];
  valueText(row, value, sizeof(value));
  gfx.setTextColor(row == confirmRow ? theme::DOWN : theme::ACCENT, bg);
  gfx.setCursor(cfg::SCREEN_W - 16 - (int16_t)strlen(value) * 12, ty);   // right-aligned
  gfx.print(value);
  strcpy(drawnValue[row], value);
}

static void drawPager(int page) {
  DFRobot_GDL& gfx = display();
  gfx.fillRect(0, PAGER_Y, cfg::SCREEN_W, PAGER_H, theme::BG);
  gfx.setTextSize(2);
  int16_t ty = PAGER_Y + (PAGER_H - 16) / 2;
  if (page > 0) {
    gfx.fillRect(0, PAGER_Y, PAGER_W, PAGER_H, theme::PANEL);
    gfx.setTextColor(theme::TEXT, theme::PANEL);
    gfx.setCursor(14, ty);
    gfx.print("< PAGE");
  }
  if (page < PAGE_COUNT - 1) {
    gfx.fillRect(cfg::SCREEN_W - PAGER_W, PAGER_Y, PAGER_W, PAGER_H, theme::PANEL);
    gfx.setTextColor(theme::TEXT, theme::PANEL);
    gfx.setCursor(cfg::SCREEN_W - PAGER_W + 14, ty);
    gfx.print("PAGE >");
  }
  char label[12];
  snprintf(label, sizeof(label), "%d / %d", page + 1, PAGE_COUNT);
  gfx.setTextColor(theme::LABEL, theme::BG);
  gfx.setCursor((cfg::SCREEN_W - (int16_t)strlen(label) * 12) / 2, ty);
  gfx.print(label);
}

// The rows of one page and the page buttons (the header stays).
static void drawPage(int page, int highlight) {
  display().fillRect(0, HEADER_H, cfg::SCREEN_W, PAGER_Y - HEADER_H, theme::BG);
  int first = page * ROWS_PER_PAGE;
  for (int i = first; i < first + ROWS_PER_PAGE && i < ROW_COUNT; i++) drawRow(i, i == highlight);
  drawPager(page);
  drawnPage = page;
  drawnHighlight = highlight;
}

int rowCount() {
  return ROW_COUNT;
}

int watchlistRow() {
  return WATCHLIST;
}

int diagnosticsRow() {
  return DIAGNOSTICS;
}

int firstRowOfPage(int page) {
  return page * ROWS_PER_PAGE;
}

int currentPage() {
  return drawnPage < 0 ? 0 : drawnPage;
}

int pageCount() {
  return PAGE_COUNT;
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
  confirmRow = -1;
  drawPage(pageOf(highlight), highlight);
}

void update(int highlight) {
  if (highlight != drawnHighlight) confirmRow = -1;   // moving away cancels a pending confirm
  if (pageOf(highlight) != drawnPage) {
    drawPage(pageOf(highlight), highlight);
    return;
  }
  int first = drawnPage * ROWS_PER_PAGE;
  for (int i = first; i < first + ROWS_PER_PAGE && i < ROW_COUNT; i++) {
    char value[24];
    valueText(i, value, sizeof(value));
    bool highlightChanged = (i == highlight) != (i == drawnHighlight);
    if (highlightChanged || strcmp(value, drawnValue[i]) != 0) drawRow(i, i == highlight);
  }
  drawnHighlight = highlight;
}

// First tap on an action row asks for a second; the second returns true (go ahead).
static bool confirmed(int row) {
  if (confirmRow != row) {
    confirmRow = row;
    return false;
  }
  confirmRow = -1;
  return true;
}

Action activate(int row, uint32_t nowMs) {
  (void)nowMs;
  Settings& st = settingsForEdit();
  int i;
  switch (row) {
    case WATCHLIST:   return Action::OpenWatchlist;
    case DIAGNOSTICS: return Action::OpenDiagnostics;
    case WIFI:        return confirmed(row) ? Action::StartWifiSetup : Action::None;
    case REFRESH:     return confirmed(row) ? Action::RefreshData : Action::None;
    case RESTART:     return confirmed(row) ? Action::Restart : Action::None;
    case AUTO_CYCLE:  st.autoCycle = !st.autoCycle; break;
    case CYCLE_SPEED: st.cycleSpeed = (st.cycleSpeed + 1) % 3; break;
    case CLICKS:      st.clickSounds = !st.clickSounds; break;
    case CHIME:       st.chime = !st.chime; break;
    case NIGHT_DIM:   st.nightDim = !st.nightDim; break;
    case ALERTS:
      i = indexOf(ALERT_STEPS, sizeof(ALERT_STEPS), st.alertPercent);
      st.alertPercent = ALERT_STEPS[(i + 1) % sizeof(ALERT_STEPS)];
      break;
    case BRIGHTNESS:
      i = indexOf(BRIGHTNESS_STEPS, sizeof(BRIGHTNESS_STEPS), st.brightness);
      st.brightness = BRIGHTNESS_STEPS[(i + 1) % sizeof(BRIGHTNESS_STEPS)];
      // app/schedule applies it on the next loop (capped during night hours, unless awake)
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
    default:
      return Action::None;
  }
  settingsSave();
  return Action::None;
}

int rowAt(int16_t x, int16_t y) {
  (void)x;
  if (y < ROW_Y0 || y >= PAGER_Y - 4) return -1;
  int k = (y - ROW_Y0) / ROW_H;
  if (k >= ROWS_PER_PAGE) return -1;
  int row = currentPage() * ROWS_PER_PAGE + k;
  return row < ROW_COUNT ? row : -1;
}

int pageTap(int16_t x, int16_t y) {
  if (y < PAGER_Y - 4) return 0;
  return x < cfg::SCREEN_W / 2 ? -1 : 1;
}

bool isBackTap(int16_t x, int16_t y) {
  return x < BACK_W + 40 && y < HEADER_H + 4;
}

}  // namespace settingsui
