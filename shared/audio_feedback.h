#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include "diagnostic_tone.h"
namespace vinyl {
// Single output-task owner: both music and idle cue PCM take this path.
class AudioFeedback {
 DiagnosticTone diagnostic;
 std::atomic<bool> request{false};
 std::atomic<unsigned> rate{44100};
 std::atomic<uint32_t> peak{0};
 uint32_t phase=0,left=0,total=0;
 unsigned previousRate=44100;
public:
 bool testTone(unsigned frequency){return diagnostic.start(frequency);}
 void stopTestTone(){diagnostic.stop();}
 bool testToneActive()const{return diagnostic.active();}
 void beep(){request.store(true);
 }
 void setRate(unsigned hz){rate.store(hz);
 }
 bool pending()const{return request.load()||left!=0||diagnostic.pending();
 } // Called only by output owner.
 uint32_t takePeak(){return peak.exchange(0);
 }
 void process(uint8_t* pcm,size_t bytes){unsigned hz=rate.load();
 if(!hz)return;
 diagnostic.begin(hz);
  if(previousRate!=hz){left=0;
  previousRate=hz;
  }
  if(request.exchange(false)){phase=0;
  total=left=hz*70/1000;
  }
  uint32_t step=uint32_t((uint64_t(880)<<32)/hz),p=0;
  for(size_t i=0;i+3<bytes;i+=4){int x=int16_t(uint16_t(pcm[i])|uint16_t(pcm[i+1])<<8);
   p=std::max(p,uint32_t(x<0?-x:x));
   if(left){unsigned t=phase>>16;
   int triangle=t<32768?int(t)*2-32768:98303-int(t)*2;
    unsigned edge=std::min(left,total-left),ramp=std::max(1u,hz/250);
    int envelope=int(std::min(edge,ramp));
    int tone=int(int64_t(triangle)*900*envelope/(32768*int64_t(ramp)));
    x=x*3/4+tone;
    phase+=step;
    --left;
   }
   if(diagnostic.sample(x))p=std::max(p,uint32_t(x<0?-x:x));
   x=std::max(-32768,std::min(32767,x));
   uint16_t v=uint16_t(int16_t(x));
   pcm[i]=pcm[i+2]=v&255;
   pcm[i+1]=pcm[i+3]=v>>8;
  }
  auto old=peak.load();
  while(old<p&&!peak.compare_exchange_weak(old,p)){}
 }
};
}
