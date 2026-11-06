#include "quote_parser.h"
#include <ArduinoJson.h>

bool parseQuote(const char* json, Quote& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;   // a non-zero error code means it failed

  // "value | fallback" gives the fallback when a field is missing or null
  // (Finnhub sends null for d and dp on unknown symbols).
  float price = doc["c"] | 0.0f;
  if (price <= 0.0f) return false;

  out.price = price;
  out.change = doc["d"] | 0.0f;
  out.changePct = doc["dp"] | 0.0f;
  out.high = doc["h"] | 0.0f;
  out.low = doc["l"] | 0.0f;
  out.open = doc["o"] | 0.0f;
  out.prevClose = doc["pc"] | 0.0f;
  out.timestamp = doc["t"] | 0u;
  return true;
}
