#include "hal/display.h"
#include <Arduino.h>
#include <Wire.h>
#include "DFRobot_Touch.h"
#include "config.h"
#include "app/input_event.h"

static DFRobot_ST7365P_320x480_HW_SPI screen(cfg::TFT_DC, cfg::TFT_CS, cfg::TFT_RST, cfg::TFT_BL);
// RST and INT are passed as "not connected" (0xFF) so the library leaves them alone:
// resetTouchController() below does the reset the way the GT911 datasheet specifies.
static DFRobot_Touch_GT911_IPS touch(cfg::GT911_ADDR, 0xFF, 0xFF);

static const uint32_t TOUCH_POLL_MS = 20;
static uint32_t lastTouchPoll = 0;
static bool wasTouched = false;
static const uint8_t RELEASE_SCANS = 3;   // empty readings in a row before a finger counts as lifted
static uint8_t emptyScans = 0;

// GT911 power-on reset. The chip picks its I2C address from the INT pin while
// coming out of reset (LOW = 0x5D), then INT must be released so the chip can use it.
static void resetTouchController() {
  pinMode(cfg::TOUCH_RST, OUTPUT);
  pinMode(cfg::TOUCH_INT, OUTPUT);
  digitalWrite(cfg::TOUCH_INT, LOW);
  digitalWrite(cfg::TOUCH_RST, LOW);
  delay(10);
  digitalWrite(cfg::TOUCH_RST, HIGH);   // address is latched here
  delay(10);
  pinMode(cfg::TOUCH_INT, INPUT);       // hand INT back to the GT911
  delay(60);                            // chip needs ~50 ms before it answers I2C
}

// Prints whether the GT911 answers, so a dead touch panel can be told apart from
// wiring, address or library problems. Only the GT911's two possible addresses are
// probed, so a stuck bus can't stall startup.
static void logTouchController() {
  Wire.setTimeOut(20);
  Serial.print("touch: probing I2C 0x5D / 0x14:");
  const uint8_t addrs[] = {0x5D, 0x14};
  int found = 0;
  for (uint8_t addr : addrs) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    Serial.printf(" 0x%02X=%s", addr, err == 0 ? "found" : (err == 2 ? "no reply" : "bus error"));
    if (err == 0) found++;
  }
  Serial.println(found ? "" : "  (check SDA 8 / SCL 9 / display power)");

  // The GT911 keeps its product ID ("911") at register 0x8140.
  char id[5] = {0};
  Wire.beginTransmission(cfg::GT911_ADDR);
  Wire.write(0x81);
  Wire.write(0x40);
  if (Wire.endTransmission() == 0 && Wire.requestFrom(cfg::GT911_ADDR, (uint8_t)4) == 4) {
    for (int i = 0; i < 4; i++) id[i] = Wire.read();
    Serial.printf("touch: chip at 0x%02X reports id \"%s\"%s\n", cfg::GT911_ADDR, id,
                  strcmp(id, "911") == 0 ? " (GT911 OK)" : " (expected \"911\")");
  } else {
    Serial.printf("touch: no reply from 0x%02X%s\n", cfg::GT911_ADDR,
                  " (if 0x14 is listed above, the chip picked its other address)");
  }
}

void displayInit() {
  // SPI clock in Hz. With no value, DFRobot_GDL defaults to 1 MHz on ESP32, slow enough
  // to watch the screen paint. 20 MHz is safe on breadboard wiring; try 40 MHz if stable.
  screen.begin(20000000);
  screen.setRotation(1);   // landscape: 480 wide x 320 tall
  screen.fillScreen(0x0000);

  resetTouchController();
  touch.begin();                      // starts I2C and reads the chip ID
  // Keep touch in portrait (rotation 0); touchPoll() converts to landscape itself.
  // The library's own landscape mode (rotation 1) computes y as 479 - raw instead of
  // 319 - raw, which reports every tap 160 px too low.
  touch.setRotation(0);
  logTouchController();
}

DFRobot_GDL& display() {
  return screen;
}

void touchPoll(uint32_t nowMs) {
  if (nowMs - lastTouchPoll < TOUCH_POLL_MS) return;
  lastTouchPoll = nowMs;

  // scan() returns "id,x,y,w,h " per finger; id 255 means nothing is touching.
  // In portrait, x runs 0-319 across and y runs 0-479 down.
  String s = touch.scan();
  int id = 255, px = 0, py = 0;
  bool touched = sscanf(s.c_str(), "%d,%d,%d", &id, &px, &py) == 3 && id != 255;

  if (touched) {
    emptyScans = 0;
    if (!wasTouched) {
      // Portrait -> landscape to match screen.setRotation(1): the long portrait axis
      // becomes x. The flips in config.h correct for how this panel is mounted.
      int16_t x = (int16_t)py;
      int16_t y = (int16_t)px;
      if (cfg::TOUCH_FLIP_X) x = (cfg::SCREEN_W - 1) - x;
      if (cfg::TOUCH_FLIP_Y) y = (cfg::SCREEN_H - 1) - y;
      inputPost(InputEvent{InputType::Tap, x, y});
      wasTouched = true;
    }
  } else if (wasTouched && ++emptyScans >= RELEASE_SCANS) {
    // The library reports "no touch" whenever the chip has no fresh data, even with a
    // finger still down. Only count the finger as lifted after several empty readings
    // in a row, so one press can't turn into several taps.
    wasTouched = false;
  }
}

void drawTouchMarker(int16_t x, int16_t y) {
  const uint16_t MAGENTA = 0xF81F;
  screen.drawFastHLine(x - 8, y, 17, MAGENTA);
  screen.drawFastVLine(x, y - 8, 17, MAGENTA);
}
