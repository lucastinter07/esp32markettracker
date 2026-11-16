#include "ui/watchlist_screen.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "data/market_data.h"
#include "data/targets.h"
#include "data/watchlist.h"
#include "hal/display.h"
#include "market_calendar.h"
#include "net/quote_client.h"
#include "net/time_sync.h"
#include "storage/history.h"
#include "symbol_text.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace watchui {

using ui::Align;

// ---------- Layout (480 x 320, layout A; the keypad as on the design canvas) ----------
static const int LIST_ROWS = NUM_SYMBOLS + 1;   // 5 stocks, 2 indexes, reset
static const int RESET_ROW = NUM_SYMBOLS;
static const int16_t NAME_X = 104;              // company name / index label in a list row
static const int16_t RULE_Y = 284, HINT_Y = 296;

// Entry box (typed ticker or price) and the one-line message under it
static const int16_t ENTRY_X = 8, ENTRY_Y = 34, ENTRY_W = 464, ENTRY_H = 40;
static const int16_t MSG_Y = 80;

// Keyboard: 3 rows of 10 keys (43 x 44, 47 px apart), then CANCEL and OK.
static const int16_t KEY_X0 = 7, KEY_W = 43, KEY_PITCH = 47;
static const int16_t KEY_Y0 = 92, KEY_H = 44, KEY_ROW_PITCH = 50;
static const int16_t BOTTOM_Y = 250, BOTTOM_H = 44, BOTTOM_W = 148, BOTTOM_PITCH = 158;
enum Key { KEY_DOT = 26, KEY_DEL = 27, KEY_CANCEL = 28, KEY_OK = 29, KEY_COUNT = 30 };

static const uint32_t LOOKUP_TIMEOUT_MS = 45000;

// Stock slot screen (8C): change ticker, alert above, alert below.
static const int SLOT_ROWS = 3;
// Number keypad for a target: 3 rows of 4 keys (1 2 3 DEL / 4 5 6 . / 7 8 9 0), then
// CANCEL, CLEAR and OK along the bottom.
static const int16_t PAD_X0 = 8, PAD_PITCH = 118, PAD_W = 110;
static const int PAD_GRID = 12, PAD_CANCEL = 12, PAD_CLEAR = 13, PAD_OK = 14, PAD_COUNT = 15;
static const char* const PAD_LABELS[PAD_GRID] = {"1", "2", "3", "DEL", "4", "5", "6", ".", "7", "8", "9", "0"};
static const int PRICE_MAX_LEN = 8;   // "12345.67"

// ---------- State ----------
enum class View : uint8_t { List, Keyboard, Checking, Confirm, Slot, Keypad };
static View view = View::List;
static int listRow = 0;
static bool resetConfirm = false;
static char drawnRow[LIST_ROWS][96];   // what each list row shows, to redraw only changes

static int slot = 0;                            // stock slot being edited
static char text[SYMBOL_MAX_LEN + 1];           // typed so far
static int keyCursor = 0;                       // highlighted key (for the buttons)
static char message[72];
static bool messageError = false;               // red instead of the normal hint color
static uint32_t lookupStartMs = 0;
static SymbolLookup found;                      // the answer being confirmed
static int slotRow = 0;                         // highlighted row on the slot screen
static bool editAbove = true;                   // which target the keypad is setting
static char priceText[PRICE_MAX_LEN + 1];       // typed on the keypad
static int padCursor = 0;                       // highlighted keypad key (for the buttons)

static void showSlot(int stockSlot);   // defined with the slot screen below

// ---------- Small helpers ----------
static const ui::Palette& P() {
  return ui::pal();
}

static void upperCase(char* s) {
  for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static bool isHeaderBackTap(const InputEvent& ev) {
  return ev.type == InputType::Tap && ev.x < 140 && ev.y < ui::BAR_H + 12;
}

// "NOW 131.52" for the header's right side ("" before the first price)
static void currentPriceNote(char* note, size_t size) {
  const SymbolState& s = stock(slot);
  if (s.hasData) snprintf(note, size, "NOW %.2f", s.quote.price);
  else note[0] = '\0';
}

static void setMessage(const char* m, bool error) {
  snprintf(message, sizeof(message), "%s", m);
  upperCase(message);
  messageError = error;
}

static void drawMessage() {
  display().fillRect(0, MSG_Y, cfg::SCREEN_W, 8, P().bg);
  ui::text(ENTRY_X, MSG_Y, message, 1, messageError ? P().down : P().dim, P().bg);
}

// The entry box: `shown` in size 3, with a cursor after it while there's room for more.
static void drawEntryBox(const char* shown, bool room) {
  DFRobot_GDL& gfx = display();
  gfx.fillRect(ENTRY_X, ENTRY_Y, ENTRY_W, ENTRY_H, P().bg);
  gfx.drawRect(ENTRY_X, ENTRY_Y, ENTRY_W, ENTRY_H, P().label);
  int16_t x = ui::text(ENTRY_X + 10, ENTRY_Y + 9, shown, 3, P().text, P().bg);
  if (room) ui::text(x, ENTRY_Y + 9, "_", 3, P().label, P().bg);
}

// ---------- List view ----------
static void rowText(int row, char* left, char* middle, char* right, uint16_t& rightColor) {
  left[0] = middle[0] = right[0] = '\0';
  rightColor = P().label;
  if (row < cfg::NUM_TICKERS) {
    const SymbolState& s = stock(row);
    strcpy(left, s.symbol);
    if (s.profile.valid) {
      snprintf(middle, 48, "%s", s.profile.name);
      upperCase(middle);
    }
    strcpy(right, "EDIT >");
  } else if (row < NUM_SYMBOLS) {
    int k = row - cfg::NUM_TICKERS;
    strcpy(left, watchlistSymbol(row));
    snprintf(middle, 48, "INDEX %d: %s", k + 1, watchlistIndexLabel(k));
    upperCase(middle);
    strcpy(right, "NEXT >");
  } else {
    strcpy(left, "RESET TO DEFAULTS");
    if (watchlistIsDefault()) {
      strcpy(right, "IN USE");
      rightColor = P().dim;
    } else if (resetConfirm) {
      strcpy(right, "TAP AGAIN");
      rightColor = P().down;
    } else {
      strcpy(right, "RESET");
    }
  }
}

static void drawListRow(int row, bool highlighted) {
  char left[24], middle[48], right[16];
  uint16_t rightColor;
  rowText(row, left, middle, right, rightColor);
  int16_t y = ui::LISTROW_Y0 + row * ui::LISTROW_PITCH;
  ui::listRow(y, left, right, rightColor, highlighted, false);
  if (middle[0]) {
    char shown[48];
    ui::fitText(middle, 1, cfg::SCREEN_W - 8 - ui::textWidth(right, 2) - 12 - NAME_X, shown, sizeof(shown));
    ui::text(NAME_X, y + 11, shown, 1, P().dim, highlighted ? P().hi : P().bg);
  }
  snprintf(drawnRow[row], sizeof(drawnRow[row]), "%d|%s|%s|%s", highlighted ? 1 : 0, left, middle, right);
}

static void updateList() {
  for (int i = 0; i < LIST_ROWS; i++) {
    char left[24], middle[48], right[16], sig[96];
    uint16_t rightColor;
    rowText(i, left, middle, right, rightColor);
    snprintf(sig, sizeof(sig), "%d|%s|%s|%s", i == listRow ? 1 : 0, left, middle, right);
    if (strcmp(sig, drawnRow[i]) != 0) drawListRow(i, i == listRow);
  }
}

static void showList() {
  view = View::List;
  resetConfirm = false;
  const ui::Palette& p = P();
  display().fillScreen(p.bg);
  ui::headerBar("< BACK  WATCHLIST", nullptr);
  for (int i = 0; i < LIST_ROWS; i++) drawListRow(i, i == listRow);
  display().drawFastHLine(0, RULE_Y, cfg::SCREEN_W, p.line);
  ui::text(8, HINT_Y, "TAP A STOCK TO CHANGE IT OR SET PRICE ALERTS, AN INDEX TO SWITCH IT", 1, p.faint, p.bg);
}

// ---------- Keyboard view ----------
static void keyRect(int k, int16_t& x, int16_t& y, int16_t& w, int16_t& h) {
  h = KEY_H;
  w = KEY_W;
  if (k < KEY_DOT) {          // letters: 10 per row
    x = KEY_X0 + (k % 10) * KEY_PITCH;
    y = KEY_Y0 + (k / 10) * KEY_ROW_PITCH;
  } else if (k == KEY_DOT) {  // after Z
    x = KEY_X0 + 6 * KEY_PITCH;
    y = KEY_Y0 + 2 * KEY_ROW_PITCH;
  } else if (k == KEY_DEL) {  // the rest of the third row
    x = KEY_X0 + 7 * KEY_PITCH;
    y = KEY_Y0 + 2 * KEY_ROW_PITCH;
    w = 3 * KEY_PITCH - (KEY_PITCH - KEY_W);
  } else {                    // CANCEL bottom left, OK bottom right
    x = k == KEY_CANCEL ? 8 : 8 + 2 * BOTTOM_PITCH;
    y = BOTTOM_Y;
    w = BOTTOM_W;
    h = BOTTOM_H;
  }
}

static void drawKey(int k) {
  int16_t x, y, w, h;
  keyRect(k, x, y, w, h);
  bool lit = k == keyCursor;
  char label[8];
  if (k < KEY_DOT) {
    label[0] = (char)('A' + k);
    label[1] = '\0';
  } else if (k == KEY_DOT) {
    strcpy(label, ".");
  } else if (k == KEY_DEL) {
    strcpy(label, "DEL");
  } else if (k == KEY_CANCEL) {
    strcpy(label, "CANCEL");
  } else {
    strcpy(label, "OK");
  }
  uint8_t size = strlen(label) == 1 ? 3 : 2;
  uint16_t color = k == KEY_CANCEL ? P().dim : P().label;
  ui::button(x, y, w, h, label, size, color, k == KEY_OK, lit);
}

static void drawEntry() {
  drawEntryBox(text, strlen(text) < SYMBOL_MAX_LEN);
}

static void drawKeyboardHeader() {
  char left[24], right[24];
  snprintf(left, sizeof(left), "< CANCEL  SLOT %d", slot + 1);
  snprintf(right, sizeof(right), "REPLACING %s", stock(slot).symbol);
  ui::headerBar(left, right);
}

static void showKeyboard() {
  view = View::Keyboard;
  display().fillScreen(P().bg);
  drawKeyboardHeader();
  drawEntry();
  drawMessage();
  for (int k = 0; k < KEY_COUNT; k++) drawKey(k);
}

static void openKeyboard(int stockSlot) {
  slot = stockSlot;
  text[0] = '\0';
  keyCursor = 0;
  setMessage("Type a ticker, then OK. It's checked with Finnhub first.", false);
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
  display().fillRect(0, ui::BAR_H, cfg::SCREEN_W, cfg::SCREEN_H - ui::BAR_H, P().bg);
}

static void showChecking() {
  view = View::Checking;
  clearBody();
  char line[32];
  snprintf(line, sizeof(line), "CHECKING %s...", text);
  ui::text(cfg::SCREEN_W / 2, 150, line, 2, P().dim, P().bg, Align::Center);
  ui::text(cfg::SCREEN_W / 2, 180, "BACK TO CANCEL", 1, P().faint, P().bg, Align::Center);
}

static void showConfirm() {
  view = View::Confirm;
  clearBody();
  const ui::Palette& p = P();
  char line[48];
  const int16_t cx = cfg::SCREEN_W / 2;
  if (found.profile.valid) {
    char name[48], shown[48];
    snprintf(name, sizeof(name), "%s", found.profile.name);
    upperCase(name);
    ui::fitText(name, 2, cfg::SCREEN_W - 16, shown, sizeof(shown));
    ui::text(cx, 96, shown, 2, p.text, p.bg, Align::Center);
  } else {
    ui::text(cx, 100, "NO COMPANY PROFILE: PROBABLY AN ETF OR FUND", 1, p.dim, p.bg, Align::Center);
  }
  snprintf(line, sizeof(line), "%s  $%.2f", found.symbol, found.quote.price);
  ui::text(cx, 128, line, 3, p.text, p.bg, Align::Center);
  snprintf(line, sizeof(line), "%+.2f (%+.2f%%) TODAY", found.quote.change, found.quote.changePct);
  ui::text(cx, 166, line, 2, found.quote.change >= 0 ? p.up : p.down, p.bg, Align::Center);
  ui::text(cx, 204, "USE THIS TICKER?", 2, p.dim, p.bg, Align::Center);
  ui::button(8, BOTTOM_Y, BOTTOM_W, BOTTOM_H, "BACK", 2, p.dim, false, false);
  snprintf(line, sizeof(line), "USE %s", found.symbol);
  ui::button(8 + 2 * BOTTOM_PITCH, BOTTOM_Y, BOTTOM_W, BOTTOM_H, line, 2, p.label, true, false);
}

static void backToKeyboard(const char* msg, bool error) {
  setMessage(msg, error);
  showKeyboard();
}

// OK pressed: check the text locally, then ask Finnhub.
static void submit(uint32_t nowMs) {
  char m[64];
  if (!isValidSymbol(text)) {
    backToKeyboard("Use letters A-Z (and a dot, as in BRK.B).", true);
    return;
  }
  int existing = watchlistFind(text);
  if (existing == slot) {
    backToKeyboard("That's already this slot's ticker.", true);
    return;
  }
  if (existing >= 0) {
    snprintf(m, sizeof(m), "%s is already on the watchlist.", text);
    backToKeyboard(m, true);
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
    slotRow = 0;
    showSlot(row);
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

// ---------- Stock slot screen: change ticker / price targets (8C) ----------
static void formatTarget(int stockIndex, bool above, char* out, size_t size, uint16_t& color) {
  PriceTarget t = targetGet(stockIndex, above);
  color = P().text;
  if (!targetSet(t)) {
    snprintf(out, size, "SET >");
    color = P().label;
  } else if (targetArmed(t)) {
    snprintf(out, size, "$%.2f", t.price);
  } else {
    int y, m, d;
    civilFromDay(t.hitDay, y, m, d);
    snprintf(out, size, "%.2f HIT %d/%d", t.price, m, d);
    color = P().dim;
  }
}

static void drawSlotRow(int row) {
  static const char* const LABELS[SLOT_ROWS] = {"CHANGE TICKER", "ALERT ABOVE", "ALERT BELOW"};
  char value[24];
  uint16_t color = P().label;
  if (row == 0) snprintf(value, sizeof(value), "EDIT >");
  else formatTarget(slot, row == 1, value, sizeof(value), color);
  ui::listRow(ui::LISTROW_Y0 + row * ui::LISTROW_PITCH, LABELS[row], value, color, row == slotRow);
}

static void showSlot(int stockSlot) {
  view = View::Slot;
  slot = stockSlot;
  const ui::Palette& p = P();
  display().fillScreen(p.bg);
  char left[24], note[24];
  snprintf(left, sizeof(left), "< BACK  %s", stock(slot).symbol);
  currentPriceNote(note, sizeof(note));
  ui::headerBar(left, note);
  for (int r = 0; r < SLOT_ROWS; r++) drawSlotRow(r);
  ui::text(8, 140, "A TARGET ALERTS ONCE (SOUND + BANNER) WHEN THE PRICE REACHES", 1, p.dim, p.bg);
  ui::text(8, 154, "IT DURING MARKET HOURS, AND SHOWS ON THE CHART AS A T TAG.", 1, p.dim, p.bg);
  ui::text(8, 168, "CHANGING THE TICKER REMOVES ITS TARGETS.", 1, p.dim, p.bg);
}

static void moveSlotRow(int to) {
  int old = slotRow;
  slotRow = (to + SLOT_ROWS) % SLOT_ROWS;
  drawSlotRow(old);
  drawSlotRow(slotRow);
}

// ---------- Number keypad for a target ----------
static void padRect(int k, int16_t& x, int16_t& y, int16_t& w, int16_t& h) {
  if (k < PAD_GRID) {
    x = PAD_X0 + (k % 4) * PAD_PITCH;
    y = KEY_Y0 + (k / 4) * KEY_ROW_PITCH;
    w = PAD_W;
    h = KEY_H;
  } else {   // CANCEL, CLEAR, OK side by side
    x = 8 + (k - PAD_CANCEL) * BOTTOM_PITCH;
    y = BOTTOM_Y;
    w = BOTTOM_W;
    h = BOTTOM_H;
  }
}

static void drawPadKey(int k) {
  int16_t x, y, w, h;
  padRect(k, x, y, w, h);
  const char* label = k < PAD_GRID ? PAD_LABELS[k] : (k == PAD_CANCEL ? "CANCEL" : (k == PAD_CLEAR ? "CLEAR" : "OK"));
  uint16_t color = k == PAD_CANCEL ? P().dim : (k == PAD_CLEAR ? P().down : P().label);
  ui::button(x, y, w, h, label, strlen(label) == 1 ? 3 : 2, color, k == PAD_OK, k == padCursor);
}

static void drawPriceEntry() {
  char shown[PRICE_MAX_LEN + 4];
  snprintf(shown, sizeof(shown), "$ %s", priceText);
  drawEntryBox(shown, strlen(priceText) < PRICE_MAX_LEN);
}

// The hint under the entry box: what the target does and which side of the price it must be.
static void padHint() {
  const SymbolState& s = stock(slot);
  char m[72];
  if (s.hasData) {
    snprintf(m, sizeof(m), "Alerts once when the price %s to this. Must be %s %.2f.", editAbove ? "rises" : "falls",
             editAbove ? "over" : "under", s.quote.price);
  } else {
    snprintf(m, sizeof(m), "Alerts once when the price %s to this.", editAbove ? "rises" : "falls");
  }
  setMessage(m, false);
}

static void showKeypad() {
  view = View::Keypad;
  display().fillScreen(P().bg);
  char left[24], note[24];
  snprintf(left, sizeof(left), "< CANCEL  %s %s", stock(slot).symbol, editAbove ? "ABOVE" : "BELOW");
  currentPriceNote(note, sizeof(note));
  ui::headerBar(left, note);
  drawPriceEntry();
  drawMessage();
  for (int k = 0; k < PAD_COUNT; k++) drawPadKey(k);
}

static void openKeypad(bool above) {
  editAbove = above;
  PriceTarget t = targetGet(slot, above);
  if (targetArmed(t)) snprintf(priceText, sizeof(priceText), "%.2f", t.price);
  else priceText[0] = '\0';
  padCursor = 0;
  padHint();
  showKeypad();
}

static int padAt(int16_t x, int16_t y) {
  for (int k = 0; k < PAD_COUNT; k++) {
    int16_t kx, ky, kw, kh;
    padRect(k, kx, ky, kw, kh);
    if (x >= kx - 2 && x < kx + kw + 2 && y >= ky - 3 && y < ky + kh + 3) return k;
  }
  return -1;
}

static void padMessage(const char* m, bool error) {
  setMessage(m, error);
  drawMessage();
}

static void submitTarget() {
  if (!priceText[0]) {
    padMessage("Type a price first (or CLEAR to remove the target).", true);
    return;
  }
  float v = strtof(priceText, nullptr);
  const SymbolState& s = stock(slot);
  float now = s.hasData ? s.quote.price : 0;
  if (!targetValid(v, editAbove, now)) {
    char m[64];
    if (!(v > 0)) snprintf(m, sizeof(m), "The price must be more than 0.");
    else snprintf(m, sizeof(m), "An %s target must be %s %.2f.", editAbove ? "above" : "below",
                  editAbove ? "over" : "under", now);
    padMessage(m, true);
    return;
  }
  targetPut(slot, editAbove, v);
  showSlot(slot);
}

static void pressPad(int k) {
  size_t len = strlen(priceText);
  if (k == PAD_CANCEL) {
    showSlot(slot);
  } else if (k == PAD_CLEAR) {
    targetPut(slot, editAbove, 0);
    showSlot(slot);
  } else if (k == PAD_OK) {
    submitTarget();
  } else if (strcmp(PAD_LABELS[k], "DEL") == 0) {
    if (len > 0) priceText[len - 1] = '\0';
    drawPriceEntry();
  } else {
    char c = PAD_LABELS[k][0];
    const char* dot = strchr(priceText, '.');
    bool tooManyDecimals = dot && (int)(len - (dot - priceText)) > 2;   // cents at most
    if (len >= (size_t)PRICE_MAX_LEN || (c == '.' && dot) || (c != '.' && tooManyDecimals)) return;
    priceText[len] = c;
    priceText[len + 1] = '\0';
    drawPriceEntry();
  }
}

static void movePadCursor(int to) {
  int old = padCursor;
  padCursor = (to + PAD_COUNT) % PAD_COUNT;
  drawPadKey(old);
  drawPadKey(padCursor);
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
        backToKeyboard(m, true);
      } else {
        backToKeyboard("Couldn't reach Finnhub. Try again.", true);
      }
    } else if (nowMs - lookupStartMs > LOOKUP_TIMEOUT_MS) {
      backToKeyboard("No answer from Finnhub yet. Try again.", true);
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
      if (ev.type == InputType::Tap && ev.y >= ui::LISTROW_Y0) {
        int row = (ev.y - ui::LISTROW_Y0) / ui::LISTROW_PITCH;
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
      if (ev.type == InputType::Back || isHeaderBackTap(ev)) backToKeyboard("Check cancelled.", false);
      break;

    case View::Slot: {
      int activate = -1;
      if (ev.type == InputType::Back || isHeaderBackTap(ev)) {
        listRow = slot;
        showList();
      } else if (ev.type == InputType::Prev) {
        moveSlotRow(slotRow - 1);
      } else if (ev.type == InputType::Next) {
        moveSlotRow(slotRow + 1);
      } else if (ev.type == InputType::Select) {
        activate = slotRow;
      } else if (ev.type == InputType::Tap && ev.y >= ui::LISTROW_Y0 &&
                 ev.y < ui::LISTROW_Y0 + SLOT_ROWS * ui::LISTROW_PITCH) {
        activate = (ev.y - ui::LISTROW_Y0) / ui::LISTROW_PITCH;
        slotRow = activate;
      }
      if (activate == 0) openKeyboard(slot);
      else if (activate > 0) openKeypad(activate == 1);
      break;
    }

    case View::Keypad:
      if (ev.type == InputType::Back || isHeaderBackTap(ev)) {
        showSlot(slot);
      } else if (ev.type == InputType::Prev) {
        movePadCursor(padCursor - 1);
      } else if (ev.type == InputType::Next) {
        movePadCursor(padCursor + 1);
      } else if (ev.type == InputType::Select) {
        pressPad(padCursor);
      } else if (ev.type == InputType::Tap) {
        int k = padAt(ev.x, ev.y);
        if (k >= 0) pressPad(k);
      }
      break;

    case View::Confirm: {
      bool tap = ev.type == InputType::Tap && ev.y >= BOTTOM_Y - 4;
      bool useTap = tap && ev.x >= cfg::SCREEN_W / 2;
      bool backTap = (tap && ev.x < cfg::SCREEN_W / 2) || isHeaderBackTap(ev);
      if (ev.type == InputType::Select || useTap) {
        useFound(nowMs);
      } else if (ev.type == InputType::Back || backTap) {
        backToKeyboard("Type a ticker, then OK.", false);
      }
      break;
    }
  }
  return Result::Stay;
}

}  // namespace watchui
