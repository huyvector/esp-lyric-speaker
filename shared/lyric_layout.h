#pragma once
#include "lyrics_core.h"
namespace vinyl {
// Fixed bounded word wrapping, measured with the actual active display font.
// Cache spans when text changes; no per-frame allocation or horizontal marquee.
struct LyricLayout {
  struct Row { uint16_t start, length; } rows[32]{};
  unsigned count = 0;
  template<class Measure> void wrap(const char* text, unsigned width, Measure measure) {
    count = 0; size_t at = 0;
    while (text[at] && count < 32) {
      while (text[at] == ' ' || text[at] == '\t') ++at;
      if (!text[at]) break;
      size_t start = at, fit = at, wordBreak = at;
      bool overflow = false;
      const char* p = text + at;
      while (*p && *p != '\n') {
        const char* previous = p; decodeUTF8(p);
        size_t next = size_t(p - text);
        char candidate[256]; size_t bytes = next - start;
        if (bytes >= sizeof candidate) { overflow = true; break; }
        memcpy(candidate, text + start, bytes); candidate[bytes] = 0;
        if (measure(candidate) > width) {
          overflow = true;
          if (fit == start) fit = next; // a single glyph must still advance
          break;
        }
        if (*previous == ' ' || *previous == '\t') wordBreak = size_t(previous - text);
        fit = next;
      }
      size_t end = fit;
      if (overflow && wordBreak > start) end = wordBreak;
      while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
      rows[count++] = {uint16_t(start), uint16_t(end - start)};
      at = end > start ? end : fit;
      while (text[at] == ' ' || text[at] == '\t' || text[at] == '\n') ++at;
    }
  }
  unsigned pages() const { return (count + 1) / 2; }
  unsigned pageAt(uint32_t elapsed, uint32_t span) const {
    return pages() ? std::min(pages() - 1, unsigned(uint64_t(elapsed) * pages() / std::max(uint32_t(1), span))) : 0;
  }
  void copyRow(const char* text, unsigned row, char* out, size_t capacity) const {
    if (!capacity) return;
    size_t n = row < count ? utf8Prefix(text + rows[row].start, std::min(size_t(rows[row].length), capacity - 1)) : 0;
    if (n) memcpy(out, text + rows[row].start, n);
    out[n] = 0;
  }
};
}
