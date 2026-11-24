#pragma once
#include <cstddef>
#include <cstring>
namespace vinyl {
inline bool asciiPrefix(const char* text, const char* prefix) {
  for (; *prefix; ++text, ++prefix) {
    unsigned char a = *text, b = *prefix;
    if (a >= 'A' && a <= 'Z') a += 32;
    if (a != b) return false;
  }
  return true;
}
// Remove only explicitly labelled guest credits. Keep remix/live/acoustic tags.
inline void cleanGuestCredits(char* text) {
  for (size_t i = 0; text[i];) {
    char open = text[i], close = open == '(' ? ')' : open == '[' ? ']' : 0;
    if (!close) { ++i; continue; }
    const char* label = text + i + 1;
    while (*label == ' ') ++label;
    if (!(asciiPrefix(label,"feat.") || asciiPrefix(label,"feat ") || asciiPrefix(label,"ft.") || asciiPrefix(label,"ft ") || asciiPrefix(label,"featuring "))) { ++i; continue; }
    size_t end = i + 1; unsigned depth = 1;
    for (; text[end] && depth; ++end) {
      if (text[end] == open) ++depth;
      else if (text[end] == close) --depth;
    }
    if (depth) { ++i; continue; } // Unclosed metadata remains literal.
    std::memmove(text + i, text + end, std::strlen(text + end) + 1);
  }
  size_t write = 0;
  for (size_t read = 0; text[read]; ++read) {
    if (text[read] == ' ' && (!write || text[write - 1] == ' ')) continue;
    text[write++] = text[read];
  }
  if (write && text[write - 1] == ' ') --write;
  text[write] = 0;
}
inline size_t primaryArtistLength(const char* artist) {
  size_t end = 0;
  while (artist[end] && artist[end] != ',' && artist[end] != ';') ++end;
  while (end && artist[end - 1] == ' ') --end;
  return end;
}
}
