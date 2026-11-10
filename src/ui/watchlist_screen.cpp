#include "ui/watchlist_screen.h"
#include <Arduino.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "data/market_data.h"
#include "data/watchlist.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "storage/history.h"
#include "symbol_text.h"
#include "ui/theme.h"

namespace watchui {

// ---------- Layout ----------
static const int16_t HEADER_H = 40;
static const int16_t BACK_W = 130;
static const int16_t ROW_Y0 = 48, ROW_H = 32;   // list: 8 rows, 48 .. 304
static const int LIST_ROWS = NUM_SYMBOLS + 1;   // 5 stocks, 2 indexes, reset
static const int RESET_ROW = NUM_SYMBOLS;

// Keyboard: 3 rows of 10 keys (44 x 44, 48 px apart), then CANCEL and OK.
static const int16_t KEY_W = 44, KEY_PITCH = 48, KEY_Y0 = 116, KEY_ROW_PITCH = 50;
static const int16_t BOTTOM_Y = 266, BOTTOM_H = 46, BOTTOM_W = 156;
static const int16_t MSG_Y = 102;
enum Key { KEY_DOT = 26, KEY_DEL = 27, KEY_CANCEL = 28, KEY_OK = 29, KEY_COUNT = 30 };

static const uint32_t LOOKUP_TIMEOUT_MS = 45000;

// ---------- State ----------
enum class View : uint8_t { List, Keyboard, Checking, Confirm };
static View view = View::List;
static int listRow = 0;
static bool resetConfirm = false;
static char drawnRow[LIST_ROWS][64];   // what each list row shows, to redraw only changes

static int slot = 0;                            // stock slot being edited
static char text[SYMBOL_MAX_LEN + 1];           // typed so far
static int keyCursor = 0;                       // highlighted key (for the buttons)
static char message[64];
static uint16_t messageColor = theme::LABEL;
static uint32_t lookupStartMs = 0;
static SymbolLookup found;                      // the answer being confirmed

// ---------- Small helpers ----------
static void printAt(int16_t x, int16_t y, uint8_t size, uint16_t fg, uint16_t bg, const char* s) {
  DFRobot_GDL& gfx = display();
  gfx.setTextSize(size);
  gfx.setTextColor(fg, bg);
  gfx.setCursor(x, y);
  gfx.print(s);
}

static void printCentered(int16_t cx, int16_t y, uint8_t size, uint16_t fg, uint16_t bg, const char* s) {
  printAt(cx - (int16_t)strlen(s) * theme::CHAR_W * size / 2, y, size, fg, bg, s);
}

// Copies at most maxChars characters of src, so long company names fit their space.
static void fitText(char* out, size_t size, const char* src, int maxChars) {
  snprintf(out, size, "%.*s", maxChars, src);
}

static void drawHeader(const char* backLabel, const char* title, const char* note) {
  DFRobot_GDL& gfx = display();
  gfx.fillRect(0, 0, cfg::SCREEN_W, HEADER_H, theme::PANEL);
  gfx.fillRect(0, 0, BACK_W, HEADER_H, theme::HIGHLIGHT);
  printAt(12, 12, 2, theme::TEXT, theme::HIGHLIGHT, backLabel);
  printAt(BACK_W + 20, 9, 3, theme::TEXT, theme::PANEL, title);
  if (note[0]) {
    printAt(cfg::SCREEN_W - 12 - (int16_t)strlen(note) * 6, 16, 1, theme::LABEL, theme::PANEL, note);
  }
}

static bool isHeaderBackTap(const InputEvent& ev) {
  return ev.type == InputType::Tap && ev.x < BACK_W + 30 && ev.y < HEADER_H + 4;
}

// ---------- List view ----------
static void rowText(int row, char* left, char* middle, char* right, uint16_t& rightColor) {
  left[0] = middle[0] = right[0] = '\0';
  rightColor = theme::ACCENT;
  if (row < cfg::NUM_TICKERS) {
    const SymbolState& s = stock(row);
    strcpy(left, s.symbol);
    fitText(middle, 48, s.profile.valid ? s.profile.name : "", 42);
    strcpy(right, "EDIT >");
  } else if (row < NUM_SYMBOLS) {
    int k = row - cfg::NUM_TICKERS;
    strcpy(left, watchlistSymbol(row));
    snprintf(middle, 48, "index %d: %s", k + 1, watchlistIndexLabel(k));
    strcpy(right, "NEXT >");
  } else {
    strcpy(left, "Reset to defaults");
    if (watchlistIsDefault()) {
      strcpy(right, "(in use)");
      rightColor = theme::DIM;
    } else {
      strcpy(right, resetConfirm ? "Tap again" : "RESET");
    }
  }
}

static void drawListRow(int row, bool highlighted) {
  DFRobot_GDL& gfx = display();
  char left[24], middle[48], right[16];
  uint16_t rightColor;
  rowText(row, left, middle, right, rightColor);
  int16_t y = ROW_Y0 + row * ROW_H;
  uint16_t bg = highlighted ? theme::HIGHLIGHT : theme::BG;
  gfx.fillRect(0, y, cfg::SCREEN_W, ROW_H - 2, bg);
  int16_t ty = y + (ROW_H - 2 - 16) / 2;
  printAt(16, ty, 2, theme::TEXT, bg, left);
  if (middle[0]) printAt(120, ty + 4, 1, theme::LABEL, bg, middle);
  printAt(cfg::SCREEN_W - 16 - (int16_t)strlen(right) * 12, ty, 2, rightColor, bg, right);
  snprintf(drawnRow[row], sizeof(drawnRow[row]), "%d|%s|%s|%s", highlighted ? 1 : 0, left, middle, right);
}

static void updateList() {
  for (int i = 0; i < LIST_ROWS; i++) {
    char left[24], middle[48], right[16], sig[64];
    uint16_t rightColor;
    rowText(i, left, middle, right, rightColor);
    snprintf(sig, sizeof(sig), "%d|%s|%s|%s", i == listRow ? 1 : 0, left, middle, right);
    if (strcmp(sig, drawnRow[i]) != 0) drawListRow(i, i == listRow);
  }
}

static void showList() {
  view = View::List;
  resetConfirm = false;
  display().fillScreen(theme::BG);
  drawHeader("< BACK", "WATCHLIST", "");
  for (int i = 0; i < LIST_ROWS; i++) drawListRow(i, i == listRow);
}

// ---------- Keyboard view ----------
static void keyRect(int k, int16_t& x, int16_t& y, int16_t& w, int16_t& h) {
  h = KEY_W;
  w = KEY_W;
  if (k < KEY_DOT) {          // letters: 10 per row
    x = 2 + (k % 10) * KEY_PITCH;
    y = KEY_Y0 + (k / 10) * KEY_ROW_PITCH;
  } else if (k == KEY_DOT) {  // after Z
    x = 2 + 6 * KEY_PITCH;
    y = KEY_Y0 + 2 * KEY_ROW_PITCH;
  } else if (k == KEY_DEL) {  // the rest of the third row
    x = 2 + 7 * KEY_PITCH;
    y = KEY_Y0 + 2 * KEY_ROW_PITCH;
    w = 3 * KEY_PITCH - 4;
  } else {                    // CANCEL bottom left, OK bottom right
    x = k == KEY_CANCEL ? 2 : cfg::SCREEN_W - 2 - BOTTOM_W;
    y = BOTTOM_Y;
    w = BOTTOM_W;
    h = BOTTOM_H;
  }
}

static void drawKey(int k) {
  DFRobot_GDL& gfx = display();
  int16_t x, y, w, h;
  keyRect(k, x, y, w, h);
  bool lit = k == keyCursor;
  uint16_t bg = lit ? theme::HIGHLIGHT : theme::PANEL;
  gfx.fillRoundRect(x, y, w, h, 4, bg);
  char label[8];
  uint16_t fg = theme::TEXT;
  if (k < KEY_DOT) {
    label[0] = (char)('A' + k);
    label[1] = '\0';
  } else if (k == KEY_DOT) {
    strcpy(label, ".");
  } else if (k == KEY_DEL) {
    strcpy(label, "DEL");
  } else if (k == KEY_CANCEL) {
    strcpy(label, "CANCEL");
    fg = theme::LABEL;
  } else {
    strcpy(label, "OK");
    fg = theme::UP;
  }
  printCentered(x + w / 2, y + (h - 16) / 2, 2, fg, bg, label);
}

static void drawEntry() {
  DFRobot_GDL& gfx = display();
  gfx.fillRect(17, 51, cfg::SCREEN_W - 34, 44, theme::BG);
  printAt(28, 58, 4, theme::TEXT, theme::BG, text);
  if (strlen(text) < SYMBOL_MAX_LEN) {   // a block cursor after the text
    gfx.fillRect(28 + (int16_t)strlen(text) * 24, 58 + 30, 20, 3, theme::ACCENT);
  }
}

static void drawMessage() {
  display().fillRect(0, MSG_Y, cfg::SCREEN_W, 10, theme::BG);
  printAt(16, MSG_Y, 1, messageColor, theme::BG, message);
}

static void setMessage(const char* m, uint16_t color) {
  strncpy(message, m, sizeof(message) - 1);
  message[sizeof(message) - 1] = '\0';
  messageColor = color;
}

static void drawKeyboardHeader() {
  char title[16], note[24];
  snprintf(title, sizeof(title), "SLOT %d", slot + 1);
  snprintf(note, sizeof(note), "replacing %s", stock(slot).symbol);
  drawHeader("< CANCEL", title, note);
}

static void showKeyboard() {
  view = View::Keyboard;
  DFRobot_GDL& gfx = display();
  gfx.fillScreen(theme::BG);
  drawKeyboardHeader();
  gfx.drawRect(16, 50, cfg::SCREEN_W - 32, 46, theme::FRAME);
  drawEntry();
  drawMessage();
  for (int k = 0; k < KEY_COUNT; k++) drawKey(k);
}

static void openKeyboard(int stockSlot) {
  slot = stockSlot;
  text[0] = '\0';
  keyCursor = 0;
  setMessage("Type a ticker, then OK. It's checked with Finnhub first.", theme::LABEL);
  showKeyboard();
}

static int keyAt(int16_t x, int16_t y) {
  for (int k = 0; k < KEY_COUNT; k++) {
    int16_t kx, ky, kw, kh;
    keyRect(k, kx, ky, kw, kh);
    // A couple of pixels of slack, so taps between keys still count.
    if (x >= kx - 2 && x < kx + kw + 2 && y >= ky - 3 && y < ky + kh + 3) return k;
  }
  return -1;
}

// ---------- Checking / confirm views (they reuse the keyboard's header) ----------
static void clearBody() {
  display().fillRect(0, MSG_Y, cfg::SCREEN_W, cfg::SCREEN_H - MSG_Y, theme::BG);
}

static void showChecking() {
  view = View::Checking;
  clearBody();
  char line[32];
  snprintf(line, sizeof(line), "Checking %s...", text);
  printCentered(cfg::SCREEN_W / 2, 170, 2, theme::LABEL, theme::BG, line);
  printCentered(cfg::SCREEN_W / 2, 200, 1, theme::DIM, theme::BG, "BACK to cancel");
}

static void drawButton(int16_t x, int16_t w, const char* label, uint16_t fg, uint16_t bg) {
  display().fillRoundRect(x, BOTTOM_Y, w, BOTTOM_H, 4, bg);
  printCentered(x + w / 2, BOTTOM_Y + (BOTTOM_H - 16) / 2, 2, fg, bg, label);
}

static void showConfirm() {
  view = View::Confirm;
  clearBody();
  char line[48];
  const int16_t cx = cfg::SCREEN_W / 2;
  if (found.profile.valid) {
    fitText(line, sizeof(line), found.profile.name, 38);
    printCentered(cx, 126, 2, theme::ACCENT, theme::BG, line);
  } else {
    printCentered(cx, 130, 1, theme::LABEL, theme::BG, "(no company profile - probably an ETF or fund)");
  }
  snprintf(line, sizeof(line), "%s  $%.2f", found.symbol, found.quote.price);
  printCentered(cx, 160, 3, theme::TEXT, theme::BG, line);
  snprintf(line, sizeof(line), "%+.2f  (%+.2f%%) today", found.quote.change, found.quote.changePct);
  printCentered(cx, 194, 2, theme::changeColor(found.quote.change), theme::BG, line);
  printCentered(cx, 228, 2, theme::LABEL, theme::BG, "Use this ticker?");
  drawButton(2, BOTTOM_W, "BACK", theme::LABEL, theme::PANEL);
  snprintf(line, sizeof(line), "USE %s", found.symbol);
  drawButton(cfg::SCREEN_W - 2 - BOTTOM_W, BOTTOM_W, line, theme::UP, theme::HIGHLIGHT);
}

static void backToKeyboard(const char* msg, uint16_t color) {
  setMessage(msg, color);
  showKeyboard();
}

// OK pressed: check the text locally, then ask Finnhub.
static void submit(uint32_t nowMs) {
  char m[64];
  if (!isValidSymbol(text)) {
    backToKeyboard("Use letters A-Z (and a dot, as in BRK.B).", theme::DOWN);
    return;
  }
  int existing = watchlistFind(text);
  if (existing == slot) {
    backToKeyboard("That's already this slot's ticker.", theme::DOWN);
    return;
  }
  if (existing >= 0) {
    snprintf(m, sizeof(m), "%s is already on the watchlist.", text);
    backToKeyboard(m, theme::DOWN);
    return;
  }
  SymbolLookup stale;
  while (quoteClientLookupResult(stale)) {}   // drop any old answer
  quoteClientLookup(text);
  lookupStartMs = nowMs;
  showChecking();
}

// The user confirmed: switch the slot and show what the check already fetched, so the
// row isn't empty while the rest (stats, history, earnings) downloads.
static void useFound(uint32_t nowMs) {
  watchlistSetStock(slot, found.symbol);
  SymbolState& s = stock(slot);
  s.applyQuote(found.quote, nowMs, marketIsOpenNow(), (uint32_t)time(nullptr));
  EtTime now;
  if (found.profile.valid && timeNowET(now)) {
    s.applyProfile(found.profile, dayNumber(now.year, now.month, now.day));
    storageSaveProfiles();
  }
  listRow = slot;
  showList();
}

static void pressKey(int k, uint32_t nowMs) {
  size_t len = strlen(text);
  if (k < KEY_DOT || k == KEY_DOT) {
    if (len < SYMBOL_MAX_LEN) {
      text[len] = k == KEY_DOT ? '.' : (char)('A' + k);
      text[len + 1] = '\0';
      drawEntry();
    }
  } else if (k == KEY_DEL) {
    if (len > 0) {
      text[len - 1] = '\0';
      drawEntry();
    }
  } else if (k == KEY_CANCEL) {
    showList();
  } else if (len > 0) {
    submit(nowMs);
  }
}

static void moveKeyCursor(int to) {
  int old = keyCursor;
  keyCursor = (to + KEY_COUNT) % KEY_COUNT;
  drawKey(old);
  drawKey(keyCursor);
}

// ---------- Index slots ----------
// Steps the index slot to the next ETF that isn't already shown somewhere.
static void nextIndexChoice(int k) {
  uint8_t current = watchlistIndexChoice(k);
  for (int step = 1; step < cfg::NUM_INDEX_CHOICES; step++) {
    uint8_t c = (current + step) % cfg::NUM_INDEX_CHOICES;
    if (watchlistFind(cfg::INDEX_CHOICE_SYMBOLS[c]) < 0) {
      watchlistSetIndexChoice(k, c);
      return;
    }
  }
}

static void activateRow(int row) {
  if (row < cfg::NUM_TICKERS) {
    openKeyboard(row);
  } else if (row < NUM_SYMBOLS) {
    nextIndexChoice(row - cfg::NUM_TICKERS);
  } else if (!watchlistIsDefault()) {
    if (!resetConfirm) {
      resetConfirm = true;
    } else {
      resetConfirm = false;
      watchlistReset();
    }
  }
}

// ---------- Public ----------
void open() {
  listRow = 0;
  showList();
}

bool busy() {
  return view == View::Checking;
}

void update(uint32_t nowMs) {
  if (view == View::List) {
    updateList();   // names fill in as profiles arrive
  } else if (view == View::Checking) {
    SymbolLookup r;
    if (quoteClientLookupResult(r) && strcmp(r.symbol, text) == 0) {
      char m[64];
      if (r.status == LookupStatus::Found) {
        found = r;
        showConfirm();
      } else if (r.status == LookupStatus::NotFound) {
        snprintf(m, sizeof(m), "Finnhub doesn't know %s. Check the spelling.", text);
        backToKeyboard(m, theme::DOWN);
      } else {
        backToKeyboard("Couldn't reach Finnhub. Try again.", theme::DOWN);
      }
    } else if (nowMs - lookupStartMs > LOOKUP_TIMEOUT_MS) {
      backToKeyboard("No answer from Finnhub yet. Try again.", theme::DOWN);
    }
  }
}

Result handle(const InputEvent& ev, uint32_t nowMs) {
  switch (view) {
    case View::List:
      if (ev.type == InputType::Back || isHeaderBackTap(ev)) return Result::Exit;
      if (ev.type == InputType::Prev) listRow = (listRow + LIST_ROWS - 1) % LIST_ROWS;
      if (ev.type == InputType::Next) listRow = (listRow + 1) % LIST_ROWS;
      if (ev.type == InputType::Prev || ev.type == InputType::Next) resetConfirm = false;
      if (ev.type == InputType::Select) activateRow(listRow);
      if (ev.type == InputType::Tap && ev.y >= ROW_Y0) {
        int row = (ev.y - ROW_Y0) / ROW_H;
        if (row < LIST_ROWS) {
          if (row != listRow) resetConfirm = false;
          listRow = row;
          activateRow(row);
        }
      }
      if (view == View::List) updateList();
      break;

    case View::Keyboard:
      if (ev.type == InputType::Back || isHeaderBackTap(ev)) {
        showList();
      } else if (ev.type == InputType::Prev) {
        moveKeyCursor(keyCursor - 1);
      } else if (ev.type == InputType::Next) {
        moveKeyCursor(keyCursor + 1);
      } else if (ev.type == InputType::Select) {
        pressKey(keyCursor, nowMs);
      } else if (ev.type == InputType::Tap) {
        int k = keyAt(ev.x, ev.y);
        if (k >= 0) pressKey(k, nowMs);
      }
      break;

    case View::Checking:
      if (ev.type == InputType::Back || isHeaderBackTap(ev)) backToKeyboard("Check cancelled.", theme::LABEL);
      break;

    case View::Confirm: {
      bool tap = ev.type == InputType::Tap && ev.y >= BOTTOM_Y - 4;
      bool useTap = tap && ev.x >= cfg::SCREEN_W / 2;
      bool backTap = (tap && ev.x < cfg::SCREEN_W / 2) || isHeaderBackTap(ev);
      if (ev.type == InputType::Select || useTap) {
        useFound(nowMs);
      } else if (ev.type == InputType::Back || backTap) {
        backToKeyboard("Type a ticker, then OK.", theme::LABEL);
      }
      break;
    }
  }
  return Result::Stay;
}

}  // namespace watchui
