#pragma once
#include <cstdint>
namespace vinyl {
// Coalesce rotary changes before writing NVS. The PCM thread only reads
// atomic EQ targets; it never accesses flash, Preferences or coefficient design.
struct ToneStore {
 unsigned bass=2;uint32_t touched=0;bool pending=false;
 void begin(unsigned low){bass=low;pending=false;}
 void changed(uint32_t now){touched=now;pending=true;}
 template<class Prefs>void tick(uint32_t now,bool blocked,unsigned low,Prefs& prefs){
  if(!pending||blocked||uint32_t(now-touched)<1000)return;
  if(low!=bass){prefs.putUInt("bass",low);bass=low;}
  pending=false;
 }
};
}
