// Turns a Finnhub /quote response into a Quote. Pure C++ (ArduinoJson also builds
// on the PC), so it's unit-tested on the PC.
//
// Example response:
//   {"c":733,"d":-33.81,"dp":-4.4092,"h":765.97,"l":732.71,"o":765.47,"pc":766.81,"t":1793912400}
#pragma once
#include "symbol_state.h"

// Returns false if the text isn't valid JSON, or if Finnhub doesn't know the symbol
// (it answers unknown symbols with "c":0 and nulls instead of an error).
bool parseQuote(const char* json, Quote& out);
