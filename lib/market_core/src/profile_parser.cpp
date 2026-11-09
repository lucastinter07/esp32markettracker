#include "profile_parser.h"
#include <ArduinoJson.h>
#include <string.h>

// Copies text into a fixed-size array, always leaving room for the ending zero byte.
static void copyText(char* dest, size_t size, const char* src) {
  strncpy(dest, src, size - 1);
  dest[size - 1] = '\0';
}

bool parseProfile(const char* json, Profile& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;

  const char* name = doc["name"] | "";
  if (name[0] == '\0') return false;   // "{}" = no profile for this symbol

  copyText(out.name, sizeof(out.name), name);
  copyText(out.industry, sizeof(out.industry), doc["finnhubIndustry"] | "");
  return true;
}
