#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace vinyl {
constexpr size_t TEXT_LIMIT = 6144, CUE_LIMIT = 320;
constexpr int ART_SIZE = 96, LABEL_SIZE = 112;
constexpr size_t ART_BYTES = ART_SIZE * ART_SIZE * 2;
constexpr size_t JSON_LIMIT = 8192;
constexpr size_t TRANSFER_LIMIT = ART_BYTES > JSON_LIMIT ? ART_BYTES : JSON_LIMIT;
static_assert(TRANSFER_LIMIT >= ART_BYTES, "The shared arena must fit decoded artwork");

inline uint32_t decodeUTF8(const char*& p) {
  uint8_t c = uint8_t(*p++);
  if (c < 128) return c;
  int n = c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
  uint32_t result = c & (n == 1 ? 31 : n == 2 ? 15 : 7);
  for (int i = 0; i < n; ++i) {
    if (!*p || (uint8_t(*p) & 0xC0) != 0x80) return '?';
    result = (result << 6) | (uint8_t(*p++) & 63);
  }
  return result;
}
inline size_t encodeUTF8(uint32_t c, char* out) {
  if (c < 128) { out[0] = c; return 1; }
  if (c < 2048) { out[0] = 0xC0 | c >> 6; out[1] = 0x80 | (c & 63); return 2; }
  if (c < 65536) { out[0] = 0xE0 | c >> 12; out[1] = 0x80 | ((c >> 6) & 63); out[2] = 0x80 | (c & 63); return 3; }
  out[0] = 0xF0 | c >> 18; out[1] = 0x80 | ((c >> 12) & 63); out[2] = 0x80 | ((c >> 6) & 63); out[3] = 0x80 | (c & 63); return 4;
}
// Compose Vietnamese combining marks in place. U8g2 renders precomposed NFC
// glyphs but does not shape combining accents. Also used to compare metadata.
inline void vietnamese(char* text, bool lowercase = false) {
  static const uint16_t rows[][6] = {
    {0x61,0xE0,0xE1,0x1EA3,0xE3,0x1EA1}, {0x103,0x1EB1,0x1EAF,0x1EB3,0x1EB5,0x1EB7}, {0xE2,0x1EA7,0x1EA5,0x1EA9,0x1EAB,0x1EAD},
    {0x65,0xE8,0xE9,0x1EBB,0x1EBD,0x1EB9}, {0xEA,0x1EC1,0x1EBF,0x1EC3,0x1EC5,0x1EC7}, {0x69,0xEC,0xED,0x1EC9,0x129,0x1ECB},
    {0x6F,0xF2,0xF3,0x1ECF,0xF5,0x1ECD}, {0xF4,0x1ED3,0x1ED1,0x1ED5,0x1ED7,0x1ED9}, {0x1A1,0x1EDD,0x1EDB,0x1EDF,0x1EE1,0x1EE3},
    {0x75,0xF9,0xFA,0x1EE7,0x169,0x1EE5}, {0x1B0,0x1EEB,0x1EE9,0x1EED,0x1EEF,0x1EF1}, {0x79,0x1EF3,0xFD,0x1EF7,0x1EF9,0x1EF5},
    {0x41,0xC0,0xC1,0x1EA2,0xC3,0x1EA0}, {0x102,0x1EB0,0x1EAE,0x1EB2,0x1EB4,0x1EB6}, {0xC2,0x1EA6,0x1EA4,0x1EA8,0x1EAA,0x1EAC},
    {0x45,0xC8,0xC9,0x1EBA,0x1EBC,0x1EB8}, {0xCA,0x1EC0,0x1EBE,0x1EC2,0x1EC4,0x1EC6}, {0x49,0xCC,0xCD,0x1EC8,0x128,0x1ECA},
    {0x4F,0xD2,0xD3,0x1ECE,0xD5,0x1ECC}, {0xD4,0x1ED2,0x1ED0,0x1ED4,0x1ED6,0x1ED8}, {0x1A0,0x1EDC,0x1EDA,0x1EDE,0x1EE0,0x1EE2},
    {0x55,0xD9,0xDA,0x1EE6,0x168,0x1EE4}, {0x1AF,0x1EEA,0x1EE8,0x1EEC,0x1EEE,0x1EF0}, {0x59,0x1EF2,0xDD,0x1EF6,0x1EF8,0x1EF4}
  };
  char* out = text; const char* p = text;
  uint32_t previous = 0; bool held = false;
  while (*p) {
    uint32_t c = decodeUTF8(p);
    int row = -1, tone = 0;
    if (held) for (int r = 0; r < 24; ++r) for (int t = 0; t < 6; ++t) if (rows[r][t] == previous) { row = r; tone = t; }
    int mark = c == 0x300 ? 1 : c == 0x301 ? 2 : c == 0x309 ? 3 : c == 0x303 ? 4 : c == 0x323 ? 5 : 0;
    if (row >= 0 && mark) { previous = rows[row][mark]; continue; }
    if (row >= 0 && (c == 0x302 || c == 0x306 || c == 0x31B)) {
      int base = row % 12, target = -1;
      if (c == 0x302) target = base == 0 ? 2 : base == 3 ? 4 : base == 6 ? 7 : -1;
      if (c == 0x306 && base == 0) target = 1;
      if (c == 0x31B) target = base == 6 ? 8 : base == 9 ? 10 : -1;
      if (target >= 0) { previous = rows[target + (row >= 12 ? 12 : 0)][tone]; continue; }
    }
    if (held) {
      if (lowercase) {
        if (row >= 12) previous = rows[row - 12][tone];
        else if (previous >= 'A' && previous <= 'Z') previous += 32;
        else if (previous == 0x110) previous = 0x111;
      }
      out += encodeUTF8(previous, out);
    }
    previous = c; held = true;
  }
  if (held) {
    if (lowercase) {
      bool found = false;
      for (int r = 12; r < 24; ++r) for (int t = 0; t < 6; ++t) if (rows[r][t] == previous) { previous = rows[r - 12][t]; found = true; }
      if (!found && previous >= 'A' && previous <= 'Z') previous += 32;
      if (previous == 0x110) previous = 0x111;
    }
    out += encodeUTF8(previous, out);
  }
  *out = 0;
}

// Comparison-only accent folding. Keep display text and persistent song keys
// untouched; iTunes often accents Vietnamese titles that AVRCP spells in ASCII.
inline void foldComparison(char* text) {
  const char* p = text; char* out = text;
  while (*p) {
    uint32_t c = decodeUTF8(p);
    if (c >= 0x1EA0 && c <= 0x1EB7) c = 'a';
    else if (c >= 0x1EB8 && c <= 0x1EC7) c = 'e';
    else if (c >= 0x1EC8 && c <= 0x1ECB) c = 'i';
    else if (c >= 0x1ECC && c <= 0x1EE3) c = 'o';
    else if (c >= 0x1EE4 && c <= 0x1EF1) c = 'u';
    else if (c >= 0x1EF2 && c <= 0x1EF9) c = 'y';
    else {
      switch (c) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0x102: case 0x103: c='a';break;
        case 0xC8: case 0xC9: case 0xCA: case 0xE8: case 0xE9: case 0xEA: c='e';break;
        case 0xCC: case 0xCD: case 0xEC: case 0xED: case 0x128: case 0x129: c='i';break;
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0x1A0: case 0x1A1: c='o';break;
        case 0xD9: case 0xDA: case 0xF9: case 0xFA: case 0x168: case 0x169: case 0x1AF: case 0x1B0: c='u';break;
        case 0xDD: case 0xFD: c='y';break;
        case 0x110: case 0x111: c='d';break;
        default: break;
      }
    }
    if (c >= 'A' && c <= 'Z') c += 32;
    out += encodeUTF8(c, out);
  }
  *out = 0;
}

inline uint32_t crc32(const uint8_t* data, size_t n) {
  uint32_t c = ~0u;
  while (n--) {
    c ^= *data++;
    for (int j = 0; j < 8; ++j) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}
inline uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline size_t utf8Prefix(const char* s, size_t maximum) {
  size_t n = strlen(s);
  if (n <= maximum) return n;
  n = maximum;
  while (n && (uint8_t(s[n]) & 0xC0) == 0x80) --n;
  return n;
}
// All packed accesses use aligned 32-bit words, including on ESP32 IRAM.
struct PackedBytes {
  volatile uint32_t* words = nullptr;
  struct Reference {
    volatile uint32_t* word; unsigned shift;
    operator char() const { return char((*word >> shift) & 255); }
    Reference& operator=(char value) { *word = (*word & ~(255u << shift)) | (uint32_t(uint8_t(value)) << shift); return *this; }
  };
  Reference operator[](size_t i) { return {words + i / 4, unsigned(i % 4) * 8}; }
  char operator[](size_t i) const { return char((words[i / 4] >> ((i % 4) * 8)) & 255); }
};
struct PlainBytes {
  char bytes[TEXT_LIMIT]{};
  char& operator[](size_t i) { return bytes[i]; }
  char operator[](size_t i) const { return bytes[i]; }
  const char* data() const { return bytes; }
};
struct Cue { int32_t ms; uint32_t text; };
struct PlainCues { Cue entries[CUE_LIMIT]{}; Cue& operator[](size_t i) { return entries[i]; } const Cue& operator[](size_t i) const { return entries[i]; } };
struct PackedCues { volatile Cue* entries = nullptr; volatile Cue& operator[](size_t i) { return entries[i]; } const volatile Cue& operator[](size_t i) const { return entries[i]; } };
template<class TextStorage = PlainBytes, class CueStorage = PlainCues>
struct BasicLyrics {
  TextStorage text;
  CueStorage cues;
  size_t count = 0, used = 0;
  bool truncated = false;
  // Input is NUL terminated, bounded by TEXT_LIMIT. Blank timed lines are kept:
  // they clear the previous lyric during an instrumental section.
  void clear() { count = used = 0; truncated = false; }
  void append(const char* input, int offset = 0) {
    for (const char* line = input; *line;) {
      const char* end = strchr(line, '\n');
      if (!end) end = line + strlen(line);
      const char* p = line;
      int32_t times[16]; size_t stamps = 0;
      while (p < end && *p == '[') {
        char* next = nullptr;
        long minutes = strtol(p + 1, &next, 10);
        if (next == p + 1 || next >= end || *next != ':' || minutes < 0 || minutes > 999) break;
        const char* secondsStart = next + 1;
        long seconds = strtol(secondsStart, &next, 10);
        if (next == secondsStart || seconds < 0 || seconds >= 60) break;
        int fraction = 0;
        if (*next == '.' || *next == ',') {
          ++next;
          int place = 100;
          while (next < end && *next >= '0' && *next <= '9') {
            if (place) { fraction += (*next - '0') * place; place /= 10; }
            ++next;
          }
        }
        if (next >= end || *next != ']') break;
        if (stamps < 16) times[stamps++] = std::max(0L, (minutes * 60 + seconds) * 1000 + fraction + offset);
        else truncated = true;
        p = next + 1;
      }
      if (stamps) {
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
        while (end > p && (end[-1] == '\r' || end[-1] == ' ')) --end;
        size_t length = end - p;
        if (used + length + 1 > TEXT_LIMIT || count + stamps > CUE_LIMIT) { truncated = true; break; }
        uint16_t index = used;
        // Strip Enhanced LRC word tags. OLED follows line timing, not karaoke.
        for (size_t i = 0; i < length; ++i) {
          if (p[i] == '<') {
            const char* close = static_cast<const char*>(memchr(p + i, '>', length - i));
            if (close) { i = close - p; continue; }
          }
          text[used++] = p[i];
        }
        text[used++] = '\0';
        for (size_t i = 0; i < stamps; ++i) { cues[count].ms = times[i]; cues[count].text = index; ++count; }
      }
      // end may have moved while trimming; locate the real next line.
      const char* newline = strchr(line, '\n');
      if (!newline) break;
      line = newline + 1;
    }
  }
  void finish(int offset = 0) {
    if (offset) for (size_t i = 0; i < count; ++i) cues[i].ms = std::max(0, int(cues[i].ms) + offset);
    // Stable insertion sort: normally already ordered, no heap workspace,
    // and no bytewise memcpy touching IRAM-resident cues.
    for (size_t i = 1; i < count; ++i) {
      int32_t time = cues[i].ms; uint32_t at = cues[i].text; size_t j = i;
      while (j && cues[j - 1].ms > time) { cues[j].ms = cues[j - 1].ms; cues[j].text = cues[j - 1].text; --j; }
      cues[j].ms = time; cues[j].text = at;
    }
  }
  void parse(const char* input) {
    clear();
    int offset = 0;
    const char* off = strstr(input, "[offset:");
    if (off) offset = std::max(-60000, std::min(60000, atoi(off + 8)));
    append(input, offset); finish();
  }
  int at(int64_t position) const {
    size_t lo = 0, hi = count;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (cues[mid].ms <= position) lo = mid + 1; else hi = mid;
    }
    return int(lo) - 1;
  }
  const char* line(int index) const { return index >= 0 && size_t(index) < count ? text.data() + cues[index].text : ""; }
  void copyLine(int index, char* out, size_t capacity) const {
    size_t n = 0;
    if (!capacity) return;
    if (index >= 0 && size_t(index) < count) {
      size_t at = cues[index].text;
      while (text[at]) {
        uint8_t c = text[at]; size_t width = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
        if (n + width >= capacity) break;
        for (size_t j = 0; j < width && text[at]; ++j) out[n++] = text[at++];
      }
    }
    out[n] = 0;
  }
};
using Lyrics = BasicLyrics<>;
using PackedLyrics = BasicLyrics<PackedBytes, PackedCues>;

struct Clock {
  uint32_t base = 0, tick = 0, duration = 0;
  bool playing = false, authoritative = false;
  uint32_t position(uint32_t now) const {
    uint64_t p = uint64_t(base) + (playing ? uint32_t(now - tick) : 0);
    return uint32_t(duration ? std::min(p, uint64_t(duration)) : std::min(p, uint64_t(UINT32_MAX)));
  }
  void setPlaying(bool value, uint32_t now) { base = position(now); tick = now; playing = value; }
  void sync(uint32_t pos, uint32_t now) {
    if (pos == UINT32_MAX) return;
    base = pos; tick = now; authoritative = true;
  }
  void reset(uint32_t now) { base = duration = 0; tick = now; authoritative = false; }
};

}
