#include "A2DPMonoQueue.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
int main() {
  for (unsigned offset=0;offset<4;++offset) for (size_t frames=1;frames<=1024;frames+=7) {
    std::array<uint8_t,4104> storage{};auto pcm=storage.data()+offset;
    for (size_t i=0;i<frames;++i) {
      uint16_t value=uint16_t(i*7919+frames*31);
      pcm[i*4]=pcm[i*4+2]=value&255;pcm[i*4+1]=pcm[i*4+3]=value>>8;
    }
    std::array<uint8_t,4096> expected{};memcpy(expected.data(),pcm,frames*4);
    size_t packed=a2dp_mono::packLeftInPlace(pcm,frames*4);assert(packed==frames*2);
    std::array<uint8_t,4096> actual{};
    for(size_t at=0;at<packed;){size_t n=std::min(size_t(512),packed-at);
      assert(a2dp_mono::expandStereo(pcm+at,n,actual.data()+at*2,actual.size()-at*2)==n*2);at+=n;}
    assert(!memcmp(actual.data(),expected.data(),frames*4));
  }
  uint8_t data[16]{},out[32];memset(out,0xA5,sizeof out);
  assert(!a2dp_mono::packLeftInPlace(data,3));assert(!a2dp_mono::expandStereo(data,3,out,sizeof out));
  assert(!a2dp_mono::expandStereo(data,16,out,31));assert(out[0]==0xA5&&out[31]==0xA5);
  // Actual configured prefill rounds up to whole incoming 2048-byte stereo
  // callbacks (1024 bytes in a mono ring). Cover 101 ms, and leave room
  // for the 4096-byte stereo burst seen in the latest runtime log.
  constexpr unsigned capacity=12288,threshold=(capacity*80/100)/4*4;
  constexpr unsigned actualFill=(threshold+1023)/1024*1024;
  static_assert(actualFill*1000/96000 > 101,"Prefill must cover observed 101ms gap at 48kHz mono");
  static_assert(capacity-actualFill>=2048,"Keep room for one 4096-byte stereo burst");
  static_assert(8192*1000/176400 < 64,"Old stereo 50% prefill did not cover the 64ms input gap");
  std::cout<<"Mono queue: unaligned/variable/signed sample round-trip, chunk bounds, invalid sizes; prefill covers measured gap in buffer model: PASS\n";
}
