#pragma once
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "eq_coefficients.h"
namespace vinyl {
// Three fixed-point biquads for a small boxed speaker: sub-bass high-pass,
// broad mid-bass bell, and a high shelf to reduce harshness. No AGC or
// envelope-driven gain: a memoryless soft knee handles rare peaks without
// release/attack pumping. Preset changes crossfade over 1536 samples.
class BassTone {
  struct Biquad {
    int32_t x1=0,x2=0,y1=0,y2=0;
    int32_t run(int32_t x, const int32_t* c) {
      int64_t sum=int64_t(c[0])*x+int64_t(c[1])*x1+int64_t(c[2])*x2-int64_t(c[3])*y1-int64_t(c[4])*y2;
      int32_t y=int32_t(std::max(-int64_t(1<<28),std::min(int64_t(1<<28),sum/(1<<28))));
      x2=x1;x1=x;y2=y1;y1=y;return y;
    }
  };
  struct Path {
    Biquad filters[3]; const TonePreset* preset=nullptr; unsigned level=0,rate=0;
    int32_t run(int32_t x) {
      if(!preset)return x;
      for(unsigned i=0;i<3;++i)x=filters[i].run(x,preset->biquad[i]);
      return int32_t(int64_t(x)*preset->preampQ15/32768);
    }
    void configure(unsigned hz,unsigned value) {
      *this=Path{};rate=hz;level=value;
      for(unsigned i=0;i<sizeof(EQ_RATES)/sizeof(*EQ_RATES);++i)
        if(EQ_RATES[i]==hz && value) {preset=&EQ_PRESETS[i][value-1];break;}
    }
  } paths[2];
  std::atomic<unsigned> requestedLevel{2},requestedRate{44100};
  std::atomic<bool> resetPending{true};
  std::atomic<uint32_t> peakInput{0},peakOutput{0},limited{0};
  unsigned active=0,fade=0;bool initialized=false;
  static void maximum(std::atomic<uint32_t>& target,uint32_t value) {
    auto old=target.load();while(old<value&&!target.compare_exchange_weak(old,value)){}
  }
public:
  struct Stats { uint32_t inputPeak,outputPeak,limitedSamples; };
  Stats takeStats(){return {peakInput.exchange(0),peakOutput.exchange(0),limited.exchange(0)};}
  void setLevel(unsigned v){requestedLevel.store(std::min(3u,v));}
  unsigned level() const{return requestedLevel.load();}
  void setRate(unsigned v){requestedRate.store(v);}
  void reset(){resetPending.store(true);}
  void process(uint8_t* pcm,size_t bytes) {
    if(!pcm||bytes%4)return;
    unsigned hz=requestedRate.load(),value=level();bool resetNow=resetPending.exchange(false);
    if(!initialized||resetNow){paths[active].configure(hz,value);fade=0;initialized=true;}
    if(!fade&&(paths[active].rate!=hz||paths[active].level!=value)) {
      paths[1-active].configure(hz,value);fade=1536;
    }
    uint32_t inPeak=0,outPeak=0,knees=0;
    for(size_t at=0;at<bytes;at+=4){
      int32_t x=int16_t(uint16_t(pcm[at])|uint16_t(pcm[at+1])<<8);
      inPeak=std::max(inPeak,uint32_t(x<0?-x:x));
      int32_t y=paths[active].run(x*256);
      if(fade){int32_t next=paths[1-active].run(x*256);
        y=int32_t((int64_t(y)*fade+int64_t(next)*(1536-fade))/1536);
        if(!--fade)active=1-active;
      }
      y/=256;
      // Flat and unsupported-rate bypass are bit-exact, including full scale.
      bool shaped=paths[active].preset||fade;
      int32_t magnitude=y<0?-y:y;
      if(shaped&&magnitude>27000){++knees;int32_t excess=magnitude-27000;
        magnitude=27000+int32_t(int64_t(5000)*excess/(5000+excess));y=y<0?-magnitude:magnitude;
      }
      y=std::max(-32768,std::min(32767,y));outPeak=std::max(outPeak,uint32_t(y<0?-y:y));
      uint16_t sample=uint16_t(int16_t(y));pcm[at]=pcm[at+2]=sample&255;pcm[at+1]=pcm[at+3]=sample>>8;
    }
    maximum(peakInput,inPeak);maximum(peakOutput,outPeak);limited.fetch_add(knees);
  }
};
// A squared volume curve reaches unity at 127, exact mute at zero. All
// setters hand off only a target; the PCM thread owns gain and slews it.
class SmoothMonoVolume {
  std::atomic<unsigned> requested{55};
  int32_t gainQ16=0;
  std::atomic<uint32_t> rawPeak{0};
public:
  void set(unsigned v){requested.store(std::min(127u,v));}
  unsigned level() const{return requested.load();}
  uint32_t takePeak(){return rawPeak.exchange(0);}
  unsigned targetQ16() const {unsigned v=level();return (v*v*65536u+8064u)/16129u;}
  void process(uint8_t* pcm,size_t bytes) {
    if(!pcm||bytes%4)return;
    int32_t target=int32_t(targetQ16());uint32_t peak=0;
    for(size_t at=0;at<bytes;at+=4){
      gainQ16+=std::max(-128,std::min(128,target-gainQ16));
      int32_t left=int16_t(uint16_t(pcm[at])|uint16_t(pcm[at+1])<<8);
      int32_t right=int16_t(uint16_t(pcm[at+2])|uint16_t(pcm[at+3])<<8);
      int32_t x=(left+right)/2;peak=std::max(peak,uint32_t(x<0?-x:x));
      uint16_t y=uint16_t(int16_t(int64_t(x)*gainQ16/65536));
      pcm[at]=pcm[at+2]=y&255;pcm[at+1]=pcm[at+3]=y>>8;
    }
    auto old=rawPeak.load();while(old<peak&&!rawPeak.compare_exchange_weak(old,peak)){}
  }
};
}
