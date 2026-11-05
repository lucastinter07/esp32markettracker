// Every pin, timing and watchlist setting in one place.
// Change a value here, and every module that uses it picks it up.
#pragma once
#include <stdint.h>

namespace cfg {

// ---- Display (ST7365P over SPI) ----
// SCK 12 / MOSI 11 / MISO 13 / CS 10 are the ESP32-S3 default SPI pins,
// which DFRobot_GDL uses automatically; listed here for reference.
constexpr uint8_t TFT_SCK  = 12;
constexpr uint8_t TFT_MOSI = 11;
constexpr uint8_t TFT_MISO = 13;
constexpr uint8_t TFT_CS   = 10;
constexpr uint8_t TFT_DC   = 15;
constexpr uint8_t TFT_RST  = 17;
constexpr uint8_t TFT_BL   = 16;

// ---- Touch (GT911 over I2C) ----
// SDA 8 / SCL 9 are the ESP32-S3 default Wire pins.
constexpr uint8_t TOUCH_SDA  = 8;
constexpr uint8_t TOUCH_SCL  = 9;
constexpr uint8_t TOUCH_INT  = 4;
constexpr uint8_t TOUCH_RST  = 5;
constexpr uint8_t GT911_ADDR = 0x5D;

constexpr int16_t SCREEN_W = 480;   // landscape
constexpr int16_t SCREEN_H = 320;

// ---- Buttons (active-low: wire each between the pin and GND) ----
constexpr uint8_t BTN_PREV   = 7;
constexpr uint8_t BTN_NEXT   = 14;
constexpr uint8_t BTN_SELECT = 6;
constexpr uint8_t BTN_BACK   = 21;
constexpr uint32_t DEBOUNCE_MS = 25;

// ---- Buzzer (passive, driven with tone()) ----
constexpr uint8_t BUZZER_PIN = 18;

// ---- Screen cycle ----
constexpr uint32_t HOME_DURATION_MS   = 30000;
constexpr uint32_t DETAIL_DURATION_MS = 15000;

// ---- Watchlist ----
constexpr int NUM_TICKERS = 5;
constexpr const char* TICKERS[NUM_TICKERS] = {"NVDA", "AMD", "INTC", "VRT", "AAPL"};

// Index ETFs stand in for the indexes (free tier has no raw index data).
constexpr int NUM_INDEXES = 2;
constexpr const char* INDEX_SYMBOLS[NUM_INDEXES] = {"SPY", "QQQ"};
constexpr const char* INDEX_LABELS[NUM_INDEXES]  = {"S&P 500", "NASDAQ"};

// Prices come from data/fake_feed until the Finnhub client exists. While true,
// the Home screen labels the prices as demo data.
constexpr bool DEMO_PRICES = true;

// Touch tuning aid: draws a magenta cross where each tap registered and logs what it
// hit. Set to false once taps land where you expect.
constexpr bool TOUCH_DEBUG = false;

// Touch orientation fixes. If the magenta cross appears mirrored from your finger,
// flip that axis: FLIP_X swaps left/right, FLIP_Y swaps top/bottom.
constexpr bool TOUCH_FLIP_X = false;
constexpr bool TOUCH_FLIP_Y = true;

}  // namespace cfg
