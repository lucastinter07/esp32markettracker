#include "hal/display.h"
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "DFRobot_Touch.h"
#include "config.h"
#include "app/input_event.h"

// ---------- fast pixel path (Phase 10C) ----------
// DFRobot_GDL sends every fill as two SPI calls per pixel and every pixel block one byte
// at a time, and opens 7 SPI transactions just to set the drawing window. Measured at
// 20 MHz: a full-screen fill took 595 ms (6.2 Mbit/s of the 20 possible) and one pixel
// 118 us. The library still sets the panel up (init commands, rotation, color mode); after
// that, all drawing goes through the functions below: one transaction per window, and the
// pixel bytes in large writes. The library sets the panel to RGB666 (3 bytes per pixel);
// cfg::DISPLAY_16BIT switches it to RGB565 (2 bytes, a third less to send).

static const uint8_t CMD_COLSET = 0x2A;   // column range
static const uint8_t CMD_ROWSET = 0x2B;   // row range
static const uint8_t CMD_RAMWR = 0x2C;    // pixel data follows
static const uint8_t CMD_COLMOD = 0x3A;   // pixel format: 0x66 = 18-bit, 0x55 = 16-bit

static constexpr bool USE_16BIT = cfg::DISPLAY_FAST_PATH && cfg::DISPLAY_16BIT;   // the library can't send 16-bit
static constexpr uint8_t BPP = USE_16BIT ? 2 : 3;

static const int FILL_PX = 480;           // one landscape row: the fill pattern buffer
static uint8_t fillBuf[FILL_PX * 3];
static int fillBufPx = 0;                 // how many pixels of fillBuf hold fillColor
static uint16_t fillColor = 0;
static int16_t protectedTop = 0;          // rows 0..protectedTop-1 are not drawn (alert banner)

// RGB565 as the panel takes it. RGB666 uses the same conversion as the library's
// rgb565ToRGB666 (5-bit red/blue widened by repeating the lowest bit).
static inline void toPanel(uint16_t c, uint8_t* out) {
  if (USE_16BIT) {
    out[0] = (uint8_t)(c >> 8);
    out[1] = (uint8_t)c;
    return;
  }
  uint8_t r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
  out[0] = (uint8_t)(((r << 1) | (r & 1)) << 2);
  out[1] = (uint8_t)(g << 2);
  out[2] = (uint8_t)(((b << 1) | (b & 1)) << 2);
}

static inline void busBegin() {
  SPI.beginTransaction(SPISettings(cfg::TFT_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(cfg::TFT_CS, LOW);
}

static inline void busEnd() {
  digitalWrite(cfg::TFT_CS, HIGH);
  SPI.endTransaction();
}

static inline void command(uint8_t cmd) {
  digitalWrite(cfg::TFT_DC, LOW);
  SPI.write(cmd);
  digitalWrite(cfg::TFT_DC, HIGH);   // what follows is data
}

// Pixels of width x height that the next RAMWR data fills, row by row.
static void window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  command(CMD_COLSET);
  SPI.write32(((uint32_t)x0 << 16) | x1);
  command(CMD_ROWSET);
  SPI.write32(((uint32_t)y0 << 16) | y1);
  command(CMD_RAMWR);
}

// The panel's driver class with the drawing calls replaced by the fast path (when
// cfg::DISPLAY_FAST_PATH is on). Adafruit_GFX declares these virtual, so lines, circles and
// the library's own text all come through here too.
class TrackerScreen : public DFRobot_ST7365P_320x480_HW_SPI {
 public:
  using DFRobot_ST7365P_320x480_HW_SPI::DFRobot_ST7365P_320x480_HW_SPI;

  void pushBlock(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* pixels) {
    if (!cfg::DISPLAY_FAST_PATH) {
      setDisplayArea((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
      sendColor(const_cast<uint8_t*>(pixels), (uint32_t)w * h * 3);
      return;
    }
    if (y < protectedTop) {   // skip the rows under the protected strip
      int16_t skip = protectedTop - y;
      if (skip >= h) return;
      pixels += (uint32_t)skip * w * BPP;
      y += skip;
      h -= skip;
    }
    busBegin();
    window(_xStart + x, _yStart + y, _xStart + x + w - 1, _yStart + y + h - 1);
    SPI.writeBytes(pixels, (uint32_t)w * h * BPP);
    busEnd();
  }

  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (!cfg::DISPLAY_FAST_PATH) {
      DFRobot_ST7365P_320x480_HW_SPI::fillRect(x, y, w, h, color);
      return;
    }
    // Clip to the screen (the library didn't, so callers stay inside; this is cheap insurance).
    if (x < 0) { w += x; x = 0; }
    if (y < protectedTop) { h -= protectedTop - y; y = protectedTop; }
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t n = (uint32_t)w * h;
    int need = n < (uint32_t)FILL_PX ? (int)n : FILL_PX;
    if (color != fillColor) {
      fillColor = color;
      fillBufPx = 0;
    }
    if (fillBufPx < need) {   // extend the pattern only as far as this fill needs
      uint8_t px[3];
      toPanel(color, px);
      for (int i = fillBufPx; i < need; i++) memcpy(fillBuf + i * BPP, px, BPP);
      fillBufPx = need;
    }
    busBegin();
    window(_xStart + x, _yStart + y, _xStart + x + w - 1, _yStart + y + h - 1);
    while (n) {
      uint32_t k = n < (uint32_t)FILL_PX ? n : FILL_PX;
      SPI.writeBytes(fillBuf, k * BPP);
      n -= k;
    }
    busEnd();
  }

  void fillScreen(uint16_t color) override { fillRect(0, 0, _width, _height, color); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override { fillRect(x, y, w, 1, color); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override { fillRect(x, y, 1, h, color); }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (!cfg::DISPLAY_FAST_PATH) {
      DFRobot_ST7365P_320x480_HW_SPI::drawPixel(x, y, color);
      return;
    }
    if (x < 0 || y < protectedTop || x >= _width || y >= _height) return;
    uint8_t px[3];
    toPanel(color, px);
    busBegin();
    window(_xStart + x, _yStart + y, _xStart + x, _yStart + y);
    SPI.writeBytes(px, BPP);
    busEnd();
  }
};

static TrackerScreen screen(cfg::TFT_DC, cfg::TFT_CS, cfg::TFT_RST, cfg::TFT_BL);
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
  // SPI clock in Hz (cfg::TFT_SPI_HZ). With no value, DFRobot_GDL defaults to 1 MHz on
  // ESP32, slow enough to watch the screen paint.
  screen.begin(cfg::TFT_SPI_HZ);
  screen.setRotation(1);   // landscape: 480 wide x 320 tall
  if (USE_16BIT) {
    // The library set 18-bit color; 16-bit sends 2 bytes per pixel instead of 3.
    busBegin();
    command(CMD_COLMOD);
    SPI.write(0x55);
    busEnd();
  }
  Serial.printf("display: SPI %lu MHz, %s, %d-bit color\n", (unsigned long)(cfg::TFT_SPI_HZ / 1000000),
                cfg::DISPLAY_FAST_PATH ? "fast path" : "library path", USE_16BIT ? 16 : 18);
  screen.fillScreen(0x0000);

  resetTouchController();
  touch.begin();                      // starts I2C and reads the chip ID
  // Keep touch in portrait (rotation 0); touchPoll() converts to landscape itself.
  // The library's own landscape mode (rotation 1) computes y as 479 - raw instead of
  // 319 - raw, which reports every tap 160 px too low.
  touch.setRotation(0);
  logTouchController();
}

void displaySetProtectedTop(int16_t rows) {
  protectedTop = rows < 0 ? 0 : rows;
}

DFRobot_GDL& display() {
  return screen;
}

uint8_t displayBytesPerPixel() {
  return BPP;
}

void displayPixelBytes(uint16_t color, uint8_t* out) {
  toPanel(color, out);
}

// One address window plus one write (ui::text sends each string as a few of these).
void displayPushPixels(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* pixels) {
  if (w <= 0 || h <= 0) return;
  screen.pushBlock(x, y, w, h, pixels);
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
