// PC stand-in for Arduino.h, for tools/preview (only what the screen code uses).
#pragma once
#include <ctype.h>
#include <time.h>
#include <math.h>

// MinGW has no localtime_r / gmtime_r (the ESP32's newlib does); single-threaded here, so
// the plain versions do.
inline struct tm* previewLocaltime(const time_t* t, struct tm* out) {
  struct tm* r = localtime(t);
  if (!r) return nullptr;
  *out = *r;
  return out;
}
inline struct tm* previewGmtime(const time_t* t, struct tm* out) {
  struct tm* r = gmtime(t);
  if (!r) return nullptr;
  *out = *r;
  return out;
}
#define localtime_r previewLocaltime
#define gmtime_r previewGmtime
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint32_t millis();

struct PreviewSerial {
  template <typename... A> void printf(const char*, A...) {}
  void println(const char*) {}
  void print(const char*) {}
};
extern PreviewSerial Serial;

template <typename T> T min(T a, T b) { return a < b ? a : b; }
template <typename T> T max(T a, T b) { return a > b ? a : b; }
