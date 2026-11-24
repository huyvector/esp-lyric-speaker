#include "lyrics_core.h"
#include <cassert>
#include <iostream>
int main() {
  using namespace vinyl;
  Lyrics l;
  l.parse("[offset:-100]\n[00:02.5]Tiếng Việt\n[00:01.20][00:03.005]hello\n[00:04.00]\n[ar:artist]\n");
  assert(l.count == 4 && l.cues[0].ms == 1100 && l.cues[1].ms == 2400 && l.cues[2].ms == 2905);
  assert(l.at(1099) == -1 && l.at(1100) == 0 && l.at(9000) == 3);
  assert(std::string(l.line(1)) == "Tiếng Việt" && std::string(l.line(3)).empty());
  l.parse("[00:01.12]<00:01.12>one <00:01.55>two\n");
  assert(std::string(l.line(0)) == "one two");
  std::string huge;
  for (int i = 0; i < 400; ++i) huge += "[00:01.00]test\n";
  l.parse(huge.c_str()); assert(l.count == CUE_LIMIT && l.truncated);
  assert(utf8Prefix("aếb", 3) == 1 && utf8Prefix("aếb", 4) == 4);
  Clock c; c.reset(UINT32_MAX - 10); c.setPlaying(true, UINT32_MAX - 10);
  assert(c.position(20) == 31);
  c.sync(1000, 20); c.setPlaying(false, 40); assert(c.position(4000) == 1020);
  c.sync(500, 4000); assert(c.position(5000) == 500);
  assert(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926);
  char vi1[] = "Tiếng Việt ĐẸP";
  vietnamese(vi1); assert(std::string(vi1) == "Tiếng Việt ĐẸP");
  vietnamese(vi1, true); assert(std::string(vi1) == "tiếng việt đẹp");
  uint32_t words[TEXT_LIMIT / 4]{}; Cue cueWords[CUE_LIMIT]{};
  PackedLyrics packed; packed.text.words = words; packed.cues.entries = cueWords;
  packed.parse("[00:03.00]Later\n[00:01.00]Tiếng Việt\n[00:02.00]\n");
  char copied[64]; packed.copyLine(0, copied, sizeof copied);
  assert(std::string(copied) == "Tiếng Việt" && packed.at(1000) == 0);
  packed.copyLine(1, copied, sizeof copied); assert(copied[0] == 0);
  packed.copyLine(2, copied, sizeof copied); assert(std::string(copied) == "Later");
  char small[3]; packed.copyLine(0, small, sizeof small); assert(std::string(small) == "Ti");
  packed.parse("[00:01.00]ếx\n"); packed.copyLine(0, small, sizeof small); assert(small[0] == 0);
  static_assert(sizeof(PackedLyrics) < 64, "Large lyric buffers must not occupy static DRAM");
  std::cout << "LRC / UTF-8 / NFC Vietnamese / clock / CRC passed\n";
}
