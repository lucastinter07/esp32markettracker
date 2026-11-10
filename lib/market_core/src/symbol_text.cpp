#include "symbol_text.h"
#include <string.h>

bool isValidSymbol(const char* text) {
  size_t len = strlen(text);
  if (len == 0 || len > SYMBOL_MAX_LEN) return false;
  if (text[0] == '.' || text[len - 1] == '.') return false;
  for (size_t i = 0; i < len; i++) {
    char c = text[i];
    if (!((c >= 'A' && c <= 'Z') || c == '.')) return false;
  }
  return true;
}

int findSymbol(const char* const* list, int count, const char* symbol) {
  for (int i = 0; i < count; i++) {
    if (strcmp(list[i], symbol) == 0) return i;
  }
  return -1;
}
