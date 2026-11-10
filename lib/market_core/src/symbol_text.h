// Rules for ticker text typed on the device. Pure C++, unit-tested on the PC.
#pragma once
#include <stddef.h>

// Longest ticker the device stores (e.g. "BRK.B" is 5); arrays need one more byte.
static const size_t SYMBOL_MAX_LEN = 7;

// 1 to SYMBOL_MAX_LEN characters, capital letters and dots only, not starting or
// ending with a dot ("BRK.B" yes, ".A", "A.", "nvda" no).
bool isValidSymbol(const char* text);

// Index of `symbol` in `list` (exact match), or -1.
int findSymbol(const char* const* list, int count, const char* symbol);
