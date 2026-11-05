#include "ui/setup_screen.h"
#include <Arduino.h>
#include <string.h>
#include "hal/display.h"
#include "net/wifi_manager.h"
#include "ui/theme.h"

namespace wifisetup {

static void text(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* s) {
  DFRobot_GDL& gfx = display();
  gfx.setTextSize(size);
  gfx.setTextColor(color, theme::BG);
  gfx.setCursor(x, y);
  gfx.print(s);
}

void drawFull() {
  DFRobot_GDL& gfx = display();
  gfx.fillScreen(theme::BG);
  char buf[48];

  text(20, 16, 3, theme::TEXT, "Wi-Fi setup");

  text(20, 62, 2, theme::DIM, "1. On your phone, join:");
  text(44, 86, 3, theme::ACCENT, wifiSetupName());

  text(20, 124, 2, theme::DIM, "2. Password:");
  text(180, 120, 3, theme::TEXT, wifiSetupPassword());

  text(20, 160, 2, theme::DIM, "3. The setup page opens by itself.");
  snprintf(buf, sizeof(buf), "   If not, visit %s", wifiSetupAddress());
  text(20, 184, 2, theme::DIM, buf);

  const char* error = wifiSetupError();
  if (error[0] != '\0') {
    // Size 2 fits 36 characters across; longer messages drop to the small font.
    text(20, 218, strlen(error) > 36 ? 1 : 2, theme::DOWN, error);
  }

  gfx.drawFastHLine(20, 248, 440, theme::PANEL);
  text(20, 262, 2, theme::TEXT, "SELECT");
  text(116, 262, 2, theme::DIM, "retry saved network");
  text(20, 288, 2, theme::TEXT, "BACK");
  text(116, 288, 2, theme::DIM, "continue offline");
}

}  // namespace wifisetup
