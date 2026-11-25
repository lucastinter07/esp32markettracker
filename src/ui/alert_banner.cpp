#include "ui/alert_banner.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "hal/display.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace banner {

static const int16_t CLOSE_W = 40;   // the X box at the right end

void draw(const AlertEvent& alert) {
  const ui::Palette& p = ui::pal();
  const SymbolState& s = stock(alert.stockIndex);
  bool up = alert.dir == AlertDirection::Up;
  uint16_t bg = alert.target ? p.target : (up ? p.up : p.down);
  display().fillRect(0, 0, cfg::SCREEN_W, HEIGHT, bg);

  char text[48];
  if (alert.target) {
    snprintf(text, sizeof(text), G_DIAMOND " %s HIT %.2f NOW %.2f", s.symbol, alert.targetPrice, s.quote.price);
  } else if (up) {
    snprintf(text, sizeof(text), G_UP " %s UP %.2f%% TODAY %.2f", s.symbol, fabsf(s.quote.changePct), s.quote.price);
  } else {
    snprintf(text, sizeof(text), G_DOWN " %s DOWN %.2f%% TODAY %.2f", s.symbol, fabsf(s.quote.changePct),
             s.quote.price);
  }
  char shown[48];
  ui::fitText(text, 2, cfg::SCREEN_W - 8 - CLOSE_W - 8, shown, sizeof(shown));   // leaves room for the X
  ui::text(8, 7, shown, 2, p.bg, bg);
  int16_t xClose = cfg::SCREEN_W - CLOSE_W;
  display().drawFastVLine(xClose, 5, HEIGHT - 10, p.bg);
  ui::text(xClose + CLOSE_W / 2, 7, "X", 2, p.bg, bg, ui::Align::Center);
}

// The X's touch area is wider than it looks (fingers are bigger than 40 px).
bool isCloseTap(int16_t x, int16_t y) {
  return y < HEIGHT + 6 && x >= cfg::SCREEN_W - CLOSE_W - 16;
}

bool isTap(int16_t x, int16_t y) {
  return y < HEIGHT + 6 && !isCloseTap(x, y);
}

}  // namespace banner
