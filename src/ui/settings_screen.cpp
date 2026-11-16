#include "ui/settings_screen.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/settings.h"
#include "hal/display.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace settingsui {

using ui::Align;

// Layout (480 x 320), as on the design canvas: 8 rows per page, then the pager.
static const int ROWS_PER_PAGE = 8;
static const int16_t RULE_Y = 284, PAGER_Y = 296;

// Page 1 = what the screen shows, page 2 = alerts, sound and system.
enum Row {
  // page 1
  WATCHLIST, AUTO_CYCLE, CYCLE_SPEED, SECTOR_MAP, OVERNIGHT_CLOCK, THEME, BRIGHTNESS, NIGHT_DIM,
  // page 2
  ALERTS, CLICKS, CHIME, WIFI, DIAGNOSTICS, REFRESH, RESTART,
  ROW_COUNT
};
static const char* const LABELS[ROW_COUNT] = {
    "WATCHLIST",    "AUTO CYCLE",   "CYCLE SPEED", "SECTOR MAP", "OVERNIGHT CLOCK", "THEME",
    "BRIGHTNESS",   "NIGHT DIMMING", "PRICE ALERTS", "CLICK SOUNDS", "CHIME",       "WI-FI",
    "DIAGNOSTICS",  "RE-DOWNLOAD DATA", "RESTART",
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

// "10P", "7A", "12A"
static void hourText(char* buf, size_t size, int hour) {
  int h12 = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(buf, size, "%d%c", h12, hour < 12 ? 'A' : 'P');
}

static void valueText(int row, char* buf, size_t size) {
  const Settings& st = settings();
  if (row == confirmRow) {
    snprintf(buf, size, "TAP AGAIN");
    return;
  }
  char a[6], b[6];
  switch (row) {
    case WATCHLIST:   snprintf(buf, size, "EDIT >"); break;
    case AUTO_CYCLE:  snprintf(buf, size, st.autoCycle ? "ON" : "OFF"); break;
    case CYCLE_SPEED: snprintf(buf, size, "%s", cycleSpeedName(st.cycleSpeed)); break;
    case SECTOR_MAP:  snprintf(buf, size, st.sectorMap == SECTOR_MAP_GRID ? "GRID" : "SIZED"); break;
    case OVERNIGHT_CLOCK: snprintf(buf, size, st.overnightClock ? "ON" : "OFF"); break;
    case THEME:       snprintf(buf, size, "%s", ui::palette(st.theme).name); break;
    case BRIGHTNESS:  snprintf(buf, size, "%d%%", st.brightness); break;
    case NIGHT_DIM:   // one row for on/off and the hours: OFF or e.g. "10P-7A"
      if (!st.nightDim) {
        snprintf(buf, size, "OFF");
      } else {
        hourText(a, sizeof(a), st.nightStart);
        hourText(b, sizeof(b), st.nightEnd);
        snprintf(buf, size, "%s-%s", a, b);
      }
      break;
    case ALERTS:
      if (st.alertPercent == 0) snprintf(buf, size, "OFF");
      else snprintf(buf, size, G_PLUSMINUS "%d%%", st.alertPercent);
      break;
    case CLICKS:      snprintf(buf, size, st.clickSounds ? "ON" : "OFF"); break;
    case CHIME:       snprintf(buf, size, st.chime ? "ON" : "OFF"); break;
    case WIFI:        snprintf(buf, size, "SETUP >"); break;
    case DIAGNOSTICS: snprintf(buf, size, ">"); break;
    case REFRESH:     snprintf(buf, size, "~1 MB >"); break;   // cellular data it will use
    case RESTART:     snprintf(buf, size, ">"); break;
    default:          buf[0] = '\0';
  }
}

static void drawRow(int row, bool highlighted) {
  char label[24], value[24];
  snprintf(label, sizeof(label), "%02d %s", row + 1, LABELS[row]);
  valueText(row, value, sizeof(value));
  const ui::Palette& p = ui::pal();
  int16_t y = ui::LISTROW_Y0 + (row % ROWS_PER_PAGE) * ui::LISTROW_PITCH;
  ui::listRow(y, label, value, row == confirmRow ? p.down : p.label, highlighted);
  strcpy(drawnValue[row], value);
}

static void drawPager(int page) {
  const ui::Palette& p = ui::pal();
  display().fillRect(0, RULE_Y, cfg::SCREEN_W, cfg::SCREEN_H - RULE_Y, p.bg);
  display().drawFastHLine(0, RULE_Y, cfg::SCREEN_W, p.line);
  ui::text(8, PAGER_Y, "< PAGE", 2, page > 0 ? p.label : p.dim, p.bg);
  char label[12];
  snprintf(label, sizeof(label), "%d / %d", page + 1, PAGE_COUNT);
  ui::text(cfg::SCREEN_W / 2, PAGER_Y, label, 2, p.dim, p.bg, Align::Center);
  ui::text(cfg::SCREEN_W - 8, PAGER_Y, "PAGE >", 2, page < PAGE_COUNT - 1 ? p.label : p.dim, p.bg, Align::Right);
}

// The rows of one page, the page number in the header and the pager.
static void drawPage(int page, int highlight) {
  char right[8];
  snprintf(right, sizeof(right), "%d/%d", page + 1, PAGE_COUNT);
  ui::headerBar("< BACK  SETTINGS", right);
  display().fillRect(0, ui::BAR_H, cfg::SCREEN_W, RULE_Y - ui::BAR_H, ui::pal().bg);
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
  display().fillScreen(ui::pal().bg);
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
    case SECTOR_MAP:  st.sectorMap = st.sectorMap == SECTOR_MAP_GRID ? SECTOR_MAP_SIZED : SECTOR_MAP_GRID; break;
    case OVERNIGHT_CLOCK: st.overnightClock = !st.overnightClock; break;
    case THEME:
      st.theme = (uint8_t)((st.theme + 1) % ui::THEME_COUNT);
      settingsSave();
      drawFull(row);   // the whole screen changes color
      return Action::None;
    case ALERTS:
      i = indexOf(ALERT_STEPS, sizeof(ALERT_STEPS), st.alertPercent);
      st.alertPercent = ALERT_STEPS[(i + 1) % sizeof(ALERT_STEPS)];
      break;
    case BRIGHTNESS:
      i = indexOf(BRIGHTNESS_STEPS, sizeof(BRIGHTNESS_STEPS), st.brightness);
      st.brightness = BRIGHTNESS_STEPS[(i + 1) % sizeof(BRIGHTNESS_STEPS)];
      // app/schedule applies it on the next loop (capped during night hours, unless awake)
      break;
    case NIGHT_DIM: {
      // OFF -> first preset -> ... -> last preset -> OFF
      int count = sizeof(NIGHT_PRESETS) / sizeof(NIGHT_PRESETS[0]);
      int current = -1;
      for (int k = 0; k < count; k++) {
        if (NIGHT_PRESETS[k][0] == st.nightStart && NIGHT_PRESETS[k][1] == st.nightEnd) current = k;
      }
      if (!st.nightDim) {
        st.nightDim = true;
        current = -1;   // start from the first preset
      } else if (current == count - 1) {
        st.nightDim = false;
        break;
      }
      int next = current + 1;
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
  if (y < ui::LISTROW_Y0 || y >= ui::LISTROW_Y0 + ROWS_PER_PAGE * ui::LISTROW_PITCH) return -1;
  int k = (y - ui::LISTROW_Y0) / ui::LISTROW_PITCH;
  int row = currentPage() * ROWS_PER_PAGE + k;
  return row < ROW_COUNT ? row : -1;
}

int pageTap(int16_t x, int16_t y) {
  if (y < RULE_Y - 4) return 0;
  return x < cfg::SCREEN_W / 2 ? -1 : 1;
}

bool isBackTap(int16_t x, int16_t y) {
  return x < 100 && y < ui::BAR_H + 12;
}

}  // namespace settingsui
