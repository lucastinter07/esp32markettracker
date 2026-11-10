// Wi-Fi connection and first-time setup.
// Saved credentials live in NVS (the ESP32's small settings storage in flash).
// With none saved, or if the saved network can't be reached at boot, the device starts
// its own hotspot ("setup mode"). Joining it from a phone opens a page to pick a network.
#pragma once
#include <stdint.h>

enum class WifiState : uint8_t {
  Connecting,   // trying the saved network
  Connected,
  Setup,        // hotspot + setup page running
};

// forceSetup = true erases the saved network first (BACK held at power-on).
void wifiInit(bool forceSetup, uint32_t nowMs);
void wifiUpdate(uint32_t nowMs);   // call every loop
void wifiRetry(uint32_t nowMs);    // from setup mode: try the saved network again
void wifiOpenSetup(uint32_t nowMs);   // from Settings: start the setup hotspot (keeps the saved network)

WifiState wifiState();
const char* wifiSetupName();       // hotspot name, e.g. "MarketTracker-A1B2"
const char* wifiSetupPassword();   // hotspot password, shown on screen
const char* wifiSetupAddress();    // setup page address, e.g. "192.168.4.1"
const char* wifiSetupError();      // why setup mode started, or "" if none
