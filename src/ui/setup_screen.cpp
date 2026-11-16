#include "ui/setup_screen.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "config.h"
#include "hal/display.h"
#include "net/wifi_manager.h"
#include "ui/gfx.h"
#include "ui/palette.h"

namespace wifisetup {

// Layout A: header bar, three numbered steps, then what the buttons do. The hotspot name
// and password are shown exactly as the phone will show them (the name has lower case).
void drawFull() {
  const ui::Palette& p = ui::pal();
  display().fillScreen(p.bg);
  ui::headerBar("WI-FI SETUP", nullptr);
  char buf[48];

  ui::text(16, 38, "1  ON YOUR PHONE, JOIN THIS WI-FI:", 2, p.dim, p.bg);
  ui::text(44, 60, wifiSetupName(), 3, p.label, p.bg);

  ui::text(16, 98, "2  PASSWORD:", 2, p.dim, p.bg);
  ui::text(180, 94, wifiSetupPassword(), 3, p.text, p.bg);

  ui::text(16, 134, "3  THE SETUP PAGE OPENS BY ITSELF.", 2, p.dim, p.bg);
  snprintf(buf, sizeof(buf), "   IF NOT, VISIT %s", wifiSetupAddress());
  ui::text(16, 158, buf, 2, p.dim, p.bg);

  const char* error = wifiSetupError();
  if (error[0] != '\0') {
    char up[96];
    snprintf(up, sizeof(up), "%s", error);
    for (char* c = up; *c; c++) *c = (char)toupper((unsigned char)*c);
    // Size 2 fits 38 characters across; longer messages drop to the small font.
    ui::text(16, 200, up, strlen(up) > 38 ? 1 : 2, p.down, p.bg);
  }

  display().drawFastHLine(0, 244, cfg::SCREEN_W, p.line);
  ui::text(16, 258, "SELECT", 2, p.label, p.bg);
  ui::text(116, 258, "RETRY SAVED NETWORK", 2, p.dim, p.bg);
  ui::text(16, 286, "BACK", 2, p.label, p.bg);
  ui::text(116, 286, "CONTINUE OFFLINE", 2, p.dim, p.bg);
}

}  // namespace wifisetup
