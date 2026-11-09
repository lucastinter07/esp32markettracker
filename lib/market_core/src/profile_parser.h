// Reads the company name and industry from Finnhub's /stock/profile2 response (~440 B).
//
// Example: {"ticker":"AMD","name":"Advanced Micro Devices, Inc.",...,"finnhubIndustry":"Semiconductors",...}
#pragma once
#include "symbol_state.h"

// False for invalid JSON, or for symbols Finnhub has no profile for (it answers "{}",
// which includes ETFs such as SPY). Text longer than the Profile fields is cut short.
bool parseProfile(const char* json, Profile& out);
