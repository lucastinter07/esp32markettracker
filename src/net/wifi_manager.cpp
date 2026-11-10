#include "net/wifi_manager.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include "net/time_sync.h"

static const uint32_t CONNECT_TIMEOUT_MS = 20000;     // give up on the saved network at boot
static const uint32_t RECONNECT_INTERVAL_MS = 30000;  // nudge reconnects after a dropout
static const uint32_t SAVE_DELAY_MS = 1500;           // let the "Saved" page reach the phone first
static const int MAX_LISTED_NETWORKS = 15;

static WifiState state = WifiState::Connecting;
static bool connectedOnce = false;     // after the first success, dropouts never trigger setup
static uint32_t stateStartMs = 0;
static uint32_t lastReconnectMs = 0;

static String savedSsid;
static String savedPass;

static char apName[24];
static char apPass[9];                 // 8 random digits (WPA2 minimum length)
static char apAddress[16] = "192.168.4.1";
static String errorText;

static bool savePending = false;
static uint32_t saveAtMs = 0;
static bool scanRunning = false;
static String networkOptions;          // <option> tags built from the last scan

// Why the last connection attempt failed. Set from the Wi-Fi driver's event task,
// so it's volatile: the main loop must always re-read it from memory.
static volatile uint8_t lastDisconnectReason = 0;
static uint8_t loggedReason = 0;

static Preferences prefs;
static DNSServer dns;
static WebServer server(80);
static bool routesAdded = false;

// ---------- saved credentials (NVS) ----------

static void loadCredentials() {
  prefs.begin("wifi", false);
  savedSsid = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();
}

static void saveCredentials(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  savedSsid = ssid;
  savedPass = pass;
}

static void clearCredentials() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();
  savedSsid = "";
  savedPass = "";
}

// ---------- setup web page ----------

// Network names are typed by strangers, so escape them before putting them in HTML.
static String htmlEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

static const char PAGE_HEAD[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Market Tracker setup</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:420px;margin:24px auto;padding:0 16px;"
    "background:#111;color:#eee}h1{font-size:22px}label{display:block;margin:18px 0 6px}"
    "input,select,button{width:100%;box-sizing:border-box;font-size:17px;padding:10px;"
    "border-radius:6px;border:1px solid #555;background:#1c1c1c;color:#eee}"
    "button{margin-top:22px;background:#ffc200;color:#111;border:0;font-weight:600}"
    ".note{color:#999;font-size:14px}.err{color:#ff6b6b}"
    "</style></head><body><h1>Market Tracker Wi-Fi</h1>";
static const char PAGE_TAIL[] = "</body></html>";

static void sendFormPage(const String& message) {
  String page = PAGE_HEAD;
  if (message.length()) page += "<p class=err>" + htmlEscape(message) + "</p>";
  page +=
      "<form method=post action=/save>"
      "<label for=pick>Nearby networks</label>"
      "<select id=pick onchange=\"document.getElementById('ssid').value=this.value\">"
      "<option value=''>";
  page += networkOptions.length() ? "Choose a network..." : "Searching... (reload in a moment)";
  page += "</option>" + networkOptions + "</select>"
      "<label for=ssid>Network name</label>"
      "<input id=ssid name=ssid maxlength=32 required autocapitalize=none autocorrect=off>"
      "<label for=pass>Password</label>"
      "<input id=pass name=pass type=password maxlength=64>"
      "<p class=note>Only 2.4 GHz networks work with the ESP32.</p>"
      "<button type=submit>Save and connect</button></form>";
  page += PAGE_TAIL;
  server.send(200, "text/html", page);
}

static void handleRoot() {
  sendFormPage(errorText);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    sendFormPage("Enter a network name (up to 32 characters).");
    return;
  }
  if (pass.length() > 0 && pass.length() < 8) {
    sendFormPage("Wi-Fi passwords are at least 8 characters.");
    return;
  }
  saveCredentials(ssid, pass);
  String page = PAGE_HEAD;
  page += "<p>Saved. The tracker is now connecting to <b>" + htmlEscape(ssid) +
          "</b>.</p><p class=note>This hotspot will close. Watch the tracker's screen: "
          "if it can't connect, the setup screen comes back with the reason.</p>";
  page += PAGE_TAIL;
  server.send(200, "text/html", page);
  savePending = true;
  saveAtMs = millis();
}

// Phones check a few known URLs to detect a login page. Sending every unknown
// address to the form makes the setup page pop up on its own.
static void handleNotFound() {
  server.sendHeader("Location", String("http://") + apAddress + "/", true);
  server.send(302, "text/plain", "");
}

// Turns the driver's numeric reason code into something actionable.
static const char* reasonText(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
      return "network not found (2.4 GHz? in range? name exact?)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "wrong password?";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_CONNECTION_FAIL:
      return "router refused the connection";
    default:
      return "no response";
  }
}

// ---------- states ----------

static void startConnect(uint32_t nowMs) {
  lastDisconnectReason = 0;
  loggedReason = 0;
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  state = WifiState::Connecting;
  stateStartMs = nowMs;
  Serial.printf("wifi: connecting to \"%s\"\n", savedSsid.c_str());
}

static void stopSetup() {
  dns.stop();
  server.stop();
  WiFi.softAPdisconnect(false);
}

static void startSetup(uint32_t nowMs, const String& reason) {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);   // AP for the phone, STA so we can scan for networks
  WiFi.softAP(apName, apPass);
  IPAddress ip = WiFi.softAPIP();
  snprintf(apAddress, sizeof(apAddress), "%s", ip.toString().c_str());

  dns.start(53, "*", ip);   // answer every name lookup with our own address
  if (!routesAdded) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    routesAdded = true;
  }
  server.begin();

  networkOptions = "";
  WiFi.scanNetworks(true);   // true = in the background
  scanRunning = true;

  errorText = reason;
  state = WifiState::Setup;
  stateStartMs = nowMs;
  Serial.printf("wifi: setup mode, join \"%s\" password %s, then open http://%s\n", apName, apPass,
                apAddress);
}

// Signal strength (RSSI, in dBm: closer to 0 is stronger) as a word people recognize.
static const char* signalLabel(int32_t rssi) {
  if (rssi >= -60) return "strong";
  if (rssi >= -70) return "good";
  if (rssi >= -80) return "weak";
  return "very weak";
}

static void collectScanResults() {
  int n = WiFi.scanComplete();
  if (n < 0) return;   // still running (-1) or failed (-2)

  // Keep one entry per network name (mesh systems and extenders repeat names),
  // using its strongest signal, then list strongest first.
  String names[MAX_LISTED_NETWORKS];
  int32_t rssis[MAX_LISTED_NETWORKS];
  int count = 0;
  for (int i = 0; i < n; i++) {
    String name = WiFi.SSID(i);
    if (name.length() == 0) continue;   // hidden network
    int32_t rssi = WiFi.RSSI(i);
    int found = -1;
    for (int j = 0; j < count; j++) {
      if (names[j] == name) found = j;
    }
    if (found >= 0) {
      if (rssi > rssis[found]) rssis[found] = rssi;
    } else if (count < MAX_LISTED_NETWORKS) {
      names[count] = name;
      rssis[count] = rssi;
      count++;
    }
  }
  // Sort strongest first (insertion sort; at most 15 entries)
  for (int i = 1; i < count; i++) {
    for (int j = i; j > 0 && rssis[j] > rssis[j - 1]; j--) {
      String tmpName = names[j];
      names[j] = names[j - 1];
      names[j - 1] = tmpName;
      int32_t tmpRssi = rssis[j];
      rssis[j] = rssis[j - 1];
      rssis[j - 1] = tmpRssi;
    }
  }

  networkOptions = "";
  for (int i = 0; i < count; i++) {
    String safe = htmlEscape(names[i]);
    networkOptions += "<option value=\"" + safe + "\">" + safe + " (" + signalLabel(rssis[i]) +
                      ")</option>";
  }
  WiFi.scanDelete();
  scanRunning = false;
}

// ---------- public ----------

void wifiInit(bool forceSetup, uint32_t nowMs) {
  WiFi.persistent(false);       // we store credentials ourselves, in our own NVS namespace
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        lastDisconnectReason = info.wifi_sta_disconnected.reason;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  uint16_t id = (uint16_t)(ESP.getEfuseMac() >> 32);   // last bytes of the chip's MAC address
  snprintf(apName, sizeof(apName), "MarketTracker-%04X", id);
  for (int i = 0; i < 8; i++) apPass[i] = '0' + (esp_random() % 10);
  apPass[8] = '\0';

  loadCredentials();
  if (forceSetup) {
    clearCredentials();
    Serial.println("wifi: BACK held at boot, saved network erased");
  }
  if (savedSsid.length() == 0) {
    startSetup(nowMs, "");
  } else {
    startConnect(nowMs);
  }
}

void wifiUpdate(uint32_t nowMs) {
  switch (state) {
    case WifiState::Setup:
      dns.processNextRequest();
      server.handleClient();
      if (scanRunning) collectScanResults();
      if (savePending && nowMs - saveAtMs >= SAVE_DELAY_MS) {
        savePending = false;
        stopSetup();
        startConnect(nowMs);
      }
      break;

    case WifiState::Connecting:
      if (lastDisconnectReason != 0 && lastDisconnectReason != loggedReason) {
        loggedReason = lastDisconnectReason;
        Serial.printf("wifi: attempt failed, reason %u: %s\n", loggedReason, reasonText(loggedReason));
      }
      if (WiFi.status() == WL_CONNECTED) {
        state = WifiState::Connected;
        connectedOnce = true;
        errorText = "";
        timeSyncStart();
        Serial.printf("wifi: connected, IP %s\n", WiFi.localIP().toString().c_str());
      } else if (!connectedOnce && nowMs - stateStartMs >= CONNECT_TIMEOUT_MS) {
        // Never connected since boot: probably a wrong password or the network is gone.
        String why = lastDisconnectReason ? reasonText(lastDisconnectReason) : "no response";
        startSetup(nowMs, "Couldn't connect to \"" + savedSsid + "\": " + why);
      } else if (connectedOnce && nowMs - lastReconnectMs >= RECONNECT_INTERVAL_MS) {
        lastReconnectMs = nowMs;
        WiFi.reconnect();
      }
      break;

    case WifiState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        state = WifiState::Connecting;   // the router dropped; auto-reconnect is on
        stateStartMs = nowMs;
        lastReconnectMs = nowMs;
        Serial.println("wifi: connection lost, reconnecting");
      }
      break;
  }
}

void wifiOpenSetup(uint32_t nowMs) {
  if (state == WifiState::Setup) return;
  // The saved network stays saved: it's only replaced if a new one is entered, and
  // SELECT on the setup screen reconnects to it.
  startSetup(nowMs, "");
}

void wifiRetry(uint32_t nowMs) {
  if (state != WifiState::Setup || savedSsid.length() == 0) return;
  stopSetup();
  startConnect(nowMs);
}

WifiState wifiState() {
  return state;
}

const char* wifiSetupName() {
  return apName;
}

const char* wifiSetupPassword() {
  return apPass;
}

const char* wifiSetupAddress() {
  return apAddress;
}

const char* wifiSetupError() {
  return errorText.c_str();
}
