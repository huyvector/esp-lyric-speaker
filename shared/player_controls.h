#pragma once
#include <algorithm>
#include <cstdint>
namespace vinyl {
enum class KeyAction : uint8_t { None, Click, Hold, Repeat };
struct Key {
 bool raw=false,down=false,held=false,armed=false;
 uint32_t edge=0,pressed=0,repeated=0;
 KeyAction tick(bool value,uint32_t now,bool repeat) {
  if(!value&&!down)armed=true;
   // A held key at boot/overlay entry never clicks.
  if(value!=raw){raw=value;
  edge=now;
  }
  if(raw!=down&&uint32_t(now-edge)>=30){down=raw;
   if(down){pressed=repeated=now;
   held=false;
   }
   else if(armed&&!held)return KeyAction::Click;
  }
  if(armed&&down&&!held&&uint32_t(now-pressed)>=650){held=true;
  repeated=now;
  return KeyAction::Hold;
  }
  if(armed&&down&&held&&repeat&&uint32_t(now-repeated)>=110){repeated=now;
  return KeyAction::Repeat;
  }
  return KeyAction::None;
 }
};
struct MemoryRetry {
 bool pending=false;
 uint32_t generation=0,checkAt=0,stableAt=0;
 bool stable=false;
 void cancel(){pending=stable=false;
 }
 void arm(uint32_t gen,uint32_t now){pending=true;
 generation=gen;
 checkAt=now+3000;
 stable=false;
 }
 bool ready(uint32_t gen,uint32_t now,uint32_t free,uint32_t largest,bool idle){
  if(!pending)return false;
  if(gen!=generation){cancel();
  return false;
  }
  if(int32_t(now-checkAt)<0)return false;
  if(free<43000||largest<19000||!idle){stable=false;
  return false;
  }
  if(!stable){stable=true;
  stableAt=now;
  return false;
  }
  if(uint32_t(now-stableAt)<1000)return false;
  cancel();
  return true;
 }
};
enum class OLEDMode:uint8_t{Auto,VU,Lyric};
inline bool useVU(OLEDMode mode,bool haveLyrics){return mode==OLEDMode::VU||(mode==OLEDMode::Auto&&!haveLyrics);
}
struct VolumePopup {
 bool visible=false,limit=false;
 uint32_t opened=0,touched=0,tween=0,shaken=0;
 int fromQ8=0,to=0,direction=1;
 static unsigned ease(uint32_t age,unsigned duration){if(age>=duration)return 65536;
 unsigned x=unsigned(uint64_t(age)*65536/duration);
 return 65536-unsigned(uint64_t(65536-x)*(65536-x)/65536);
 }
 int valueQ8(uint32_t now)const{return fromQ8+int(int64_t(to*256-fromQ8)*ease(now-tween,140)/65536);
 }
 void show(int old,int value,bool hit,int sign,uint32_t now){bool continuing=visible&&uint32_t(now-touched)<1300;
  fromQ8=continuing?valueQ8(now):old*256;
  to=value;
  tween=touched=now;
  if(!continuing)opened=now;
  if(hit&&(!limit||uint32_t(now-shaken)>=280)){shaken=now;
  direction=sign;
  }
  limit=hit;
  visible=true;
 }
 bool tick(uint32_t now){if(visible&&uint32_t(now-touched)>=1300){visible=false;
 return true;
 }return false;
 }
 int shake(uint32_t now)const{static const int8_t x[]={3,-3,2,-2,1,-1,0};
 unsigned age=now-shaken;
 return limit&&age<280?x[std::min(6u,age/40)]*direction:0;
 }
 int slide(uint32_t now)const{unsigned age=now-touched;
 unsigned e=ease(now-opened,180);
 if(age>1120)e=ease(1300-age,180);
 return int(32*(65536-e)/65536);
 }
};
}
