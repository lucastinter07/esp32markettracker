#include "ui/tab_bar.h"
#include "config.h"
#include "hal/display.h"
#include "ui/fonts.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace tabbar {

static const char* const LABELS[COUNT] = {"Home", "Sectors", "Clock", "Settings"};

void draw(Tab active) {
  DFRobot_GDL& gfx = display();
  const ui::Palette& p = ui::pal();
  int16_t w = cfg::SCREEN_W / COUNT;
  gfx.fillRect(0, Y, cfg::SCREEN_W, H, p.nav);
  gfx.drawFastHLine(0, Y, cfg::SCREEN_W, p.border);
  for (int i = 0; i < COUNT; i++) {
    bool on = i == active;
    if (on) gfx.fillRect(i * w, Y, w, 2, p.accent);
    ui::textMid(i * w + w / 2, Y, H, InterSemi12, on ? p.accent : p.faint, LABELS[i], ui::Align::Center);
  }
}

int tabAt(int16_t x, int16_t y) {
  if (y < Y - 6) return -1;   // a little extra room above the bar for a fingertip
  int i = x / (cfg::SCREEN_W / COUNT);
  return i < COUNT ? i : COUNT - 1;
}

}  // namespace tabbar
