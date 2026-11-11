#include "ui/alert_banner.h"
#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "data/market_data.h"
#include "hal/display.h"
#include "ui/theme.h"

namespace banner {

void draw(int stockIndex, AlertDirection dir) {
  DFRobot_GDL& gfx = display();
  const SymbolState& s = stock(stockIndex);
  bool up = dir == AlertDirection::Up;
  uint16_t bg = up ? theme::UP : theme::DOWN;
  gfx.fillRect(0, 0, cfg::SCREEN_W, HEIGHT, bg);

  // Arrow pointing the way the price moved
  if (up) gfx.fillTriangle(10, 22, 24, 22, 17, 8, theme::BG);
  else gfx.fillTriangle(10, 8, 24, 8, 17, 22, theme::BG);

  char text[48];
  snprintf(text, sizeof(text), "%s %s %.2f%% today, $%.2f", s.symbol, up ? "up" : "down",
           fabsf(s.quote.changePct), s.quote.price);
  gfx.setTextSize(2);
  gfx.setTextColor(theme::BG, bg);
  gfx.setCursor(34, 8);
  gfx.print(text);
  int16_t hintX = cfg::SCREEN_W - 10 - 11 * 6;
  if (34 + (int16_t)strlen(text) * 12 + 8 <= hintX) {   // only if a long ticker leaves room
    gfx.setTextSize(1);
    gfx.setCursor(hintX, 12);
    gfx.print("TAP TO VIEW");
  }
}

bool isTap(int16_t x, int16_t y) {
  (void)x;
  return y < HEIGHT + 6;
}

}  // namespace banner
