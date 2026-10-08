#include <Arduino.h>
#include <string.h>
#include "DFRobot_GDL.h"

#define TFT_DC   25
#define TFT_CS   5
#define TFT_RST  26
#define TFT_BL   4 

DFRobot_ST7365P_320x480_HW_SPI screen(/*dc=*/TFT_DC, /*cs=*/TFT_CS, /*rst=*/TFT_RST, /*bl=*/TFT_BL);

const char *stock_list[] = {"NVDA", "AMD", "INTC", "VRT"};

void setup() {
  Serial.begin(115200);
  screen.begin();
  Serial.println("screen.begin() done");
  screen.fillScreen(COLOR_RGB565_BLACK);

  // Outline rectangle: x, y, width, height, color
  screen.drawRect(40, 60, 240, 150, COLOR_RGB565_WHITE);

  // Filled rectangle
  screen.fillRect(80, 260, 160, 100, COLOR_RGB565_BLUE);

  // Rounded rectangle (last-but-one arg is corner radius)
  screen.fillRoundRect(60, 390, 200, 60, 15, COLOR_RGB565_ORANGE);
}

void loop() {
}
