// PC stand-in for the ESP32 WiFi library, for tools/preview (what the Diagnostics screen
// reads: connected, network name, signal, IP address). Sample values.
#pragma once
#include <string>

struct PreviewString {
  std::string s;
  const char* c_str() const { return s.c_str(); }
};
struct PreviewIp {
  PreviewString toString() const { return {"172.20.10.4"}; }
};
struct PreviewWiFi {
  bool isConnected() const { return true; }
  PreviewString SSID() const { return {"iPhone"}; }
  int RSSI() const { return -58; }
  PreviewIp localIP() const { return {}; }
};
extern PreviewWiFi WiFi;
