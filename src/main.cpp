// Entry point. This file only starts each module and runs the main loop.
// The actual work lives in the folders under src/ (see CLAUDE.md for the layout).
#include <Arduino.h>
#include "app/input_event.h"
#include "app/screen_manager.h"
#include "config.h"
#include "data/fake_feed.h"
#include "data/market_data.h"
#include "hal/buttons.h"
#include "hal/buzzer.h"
#include "hal/display.h"
#include "net/quote_client.h"
#include "net/wifi_manager.h"
#include "storage/history.h"

static ScreenManager screens;

void setup() {
  Serial.begin(115200);
  // The USB serial port disconnects on every reset. Wait up to 2 s for the PC's
  // serial monitor to reconnect so the startup messages aren't lost.
  while (!Serial && millis() < 2000) delay(10);
  uint32_t now = millis();

  inputInit();
  displayInit();
  buttonsInit();
  buzzerInit();
  marketDataInit();
  storageInit();
  if (cfg::DEMO_PRICES) fakeFeedInit(now);
  else quoteClientStart();   // background task; waits for Wi-Fi by itself

  // Holding BACK while powering on erases the saved network and opens Wi-Fi setup.
  wifiInit(buttonsBackHeld(), now);
  screens.begin(now);

  buzzerChime();
  Serial.println("market tracker started");
}

void loop() {
  uint32_t now = millis();
  buttonsPoll(now);
  touchPoll(now);
  wifiUpdate(now);
  if (cfg::DEMO_PRICES) fakeFeedUpdate(now);
  else quoteClientApply(now);
  storageUpdate(now);
  screens.update(now);
  delay(5);
}
