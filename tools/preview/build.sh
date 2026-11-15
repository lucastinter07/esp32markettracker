#!/bin/sh
# Builds and runs the PC screen previews: tools/preview/build.sh <output folder> [theme 0-4]
# Needs a native g++ (MinGW on Windows). Run from the repo root.
set -e
OUT="${1:-preview_out}"
mkdir -p "$OUT"
JSON=".pio/libdeps/native/ArduinoJson/src"
g++ -std=gnu++14 -O1 -w \
  -I tools/preview/stubs -I src -I include -I lib/market_core/src -I "$JSON" \
  tools/preview/preview.cpp \
  src/ui/gfx.cpp src/ui/fonts.cpp src/ui/palette.cpp src/ui/tab_bar.cpp src/ui/status_bar.cpp \
  src/ui/home_screen.cpp src/ui/splash_screen.cpp \
  lib/market_core/src/market_calendar.cpp lib/market_core/src/price_series.cpp \
  lib/market_core/src/symbol_state.cpp lib/market_core/src/earnings.cpp lib/market_core/src/history.cpp \
  -o "$OUT/preview.exe"
"$OUT/preview.exe" "$OUT" "${2:-0}"
