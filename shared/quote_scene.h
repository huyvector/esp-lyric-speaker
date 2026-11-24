#pragma once
#include "lyric_layout.h"
#include <initializer_list>
namespace vinyl {
// All layouts fit the 246 x 50 body region. Base Unifont cell is 8 x 16;
// the sampled cell includes two extra rows for Vietnamese marks/descenders.
struct QuoteLayout {
 LyricLayout lines;unsigned scale=256;
 template<class Measure>void wrap(const char*text,Measure measure){
  unsigned width=measure(text);
  if(width<=123){scale=512;lines.wrap(text,123,measure);return;}
  if(width<=164){scale=384;lines.wrap(text,164,measure);return;}
  for(unsigned candidate:{320u,288u,256u}){
   lines.wrap(text,246u*256/candidate,measure);scale=candidate;
   if(lines.count<=2||candidate==256)return;
  }
 }
 unsigned rows(unsigned page)const{return std::min(2u,lines.count>page*2?lines.count-page*2:0u);}
};
inline unsigned easeOutQ8(uint32_t elapsed,unsigned span){if(elapsed>=span)return 256;unsigned left=256-elapsed*256/span;return 256-(left*left*left>>16);}
inline uint16_t mix565(uint16_t from,uint16_t to,unsigned alpha){alpha=std::min(256u,alpha);unsigned inverse=256-alpha;
 unsigned r=((from>>11)*inverse+(to>>11)*alpha+128)>>8;
 unsigned g=(((from>>5)&63)*inverse+((to>>5)&63)*alpha+128)>>8;
 unsigned b=((from&31)*inverse+(to&31)*alpha+128)>>8;return uint16_t(r<<11|g<<5|b);
}
struct QuoteMotion {
 uint32_t changed=0;unsigned duration=320,delay=45,exit=200;bool active=false;
 void start(uint32_t now,uint32_t remaining=2000){changed=now;duration=std::min(320u,std::max(80u,unsigned(remaining/4)));delay=std::min(45u,duration/8);exit=std::min(200u,duration*2/3);active=true;}
 unsigned incoming(uint32_t now,unsigned row=0)const {uint32_t elapsed=now-changed,wait=row*delay;if(!active)return 256;return elapsed<wait?0:easeOutQ8(elapsed-wait,duration);}
 unsigned outgoing(uint32_t now)const{uint32_t elapsed=now-changed;return !active||elapsed>=exit?0:256-unsigned(elapsed)*256/exit;}
 bool moving(uint32_t now)const{return active&&uint32_t(now-changed)<duration+delay+20;}
};
}
