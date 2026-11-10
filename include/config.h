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
constexpr uint32_t LONG_PRESS_MS = 2000;   // holding BACK this long opens Settings

// ---- Buzzer (passive, driven with tone()) ----
constexpr uint8_t BUZZER_PIN = 18;

// ---- Screen cycle ----
constexpr uint32_t HOME_DURATION_MS   = 30000;
constexpr uint32_t SPLASH_MIN_MS      = 2500;    // startup screen shows at least this long
constexpr uint32_t SPLASH_MAX_MS      = 20000;   // ...and at most this long, even if prices haven't arrived
constexpr uint32_t DETAIL_DURATION_MS = 20000;   // auto mode: chart page, then stats page halfway

// ---- Watchlist ----
// These are the defaults: the first boot (and "Reset to defaults") uses them. Changes made
// on the device (Settings > Watchlist) are saved in NVS and win over these after that.
constexpr int NUM_TICKERS = 5;
constexpr const char* TICKERS[NUM_TICKERS] = {"NVDA", "AMD", "INTC", "VRT", "AAPL"};

// Index ETFs stand in for the indexes (free tier has no raw index data). Each index slot
// can show one of these; the defaults are the first two.
constexpr int NUM_INDEXES = 2;
constexpr int NUM_INDEX_CHOICES = 4;
constexpr const char* INDEX_CHOICE_SYMBOLS[NUM_INDEX_CHOICES] = {"SPY", "QQQ", "DIA", "IWM"};
constexpr const char* INDEX_CHOICE_LABELS[NUM_INDEX_CHOICES]  = {"S&P 500", "NASDAQ", "DOW", "RUSSELL 2000"};
constexpr uint8_t DEFAULT_INDEX_CHOICES[NUM_INDEXES] = {0, 1};   // SPY, QQQ

// true: made-up prices from data/fake_feed (no network needed), labelled DEMO PRICES.
// false: live prices from Finnhub (net/quote_client), using the key in .env.
constexpr bool DEMO_PRICES = false;

// While the market is open, each symbol is refreshed about this often. Every request
// uses hotspot data: 60 s works out to roughly 50-60 MB a month of trading days.
// Finnhub's free limit is 60 requests/min, so don't go below ~10 s.
constexpr uint32_t QUOTE_REFRESH_S = 60;

// While the market is open, a price that hasn't updated for this long is shown grey
// and the Home screen says PRICES DELAYED (three missed refreshes).
constexpr uint32_t STALE_AFTER_S = QUOTE_REFRESH_S * 3;

// Company stats (52-week range, P/E, market cap) come from a ~196 KB response, so
// they're fetched only this often and kept current from live prices in between.
constexpr int METRICS_REFRESH_DAYS = 7;

// Company names (Finnhub /stock/profile2, ~440 B each) rarely change: refetched monthly.
constexpr int PROFILE_REFRESH_DAYS = 30;

// While the market is open, today's chart points are saved to flash this often, so a
// restart doesn't lose the day's chart.
constexpr uint32_t HISTORY_SAVE_INTERVAL_S = 300;

// Touch tuning aid: draws a magenta cross where each tap registered and logs what it
// hit. Set to false once taps land where you expect.
constexpr bool TOUCH_DEBUG = false;

// Touch orientation fixes. If the magenta cross appears mirrored from your finger,
// flip that axis: FLIP_X swaps left/right, FLIP_Y swaps top/bottom.
constexpr bool TOUCH_FLIP_X = false;
constexpr bool TOUCH_FLIP_Y = true;

}  // namespace cfg
