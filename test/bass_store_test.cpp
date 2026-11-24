#include "tone_store.h"
#include <cassert>
#include <iostream>
struct Prefs{unsigned writes=0,value=0;void putUInt(const char*,unsigned v){++writes;value=v;}};
int main(){Prefs p;vinyl::ToneStore s;s.begin(3);s.tick(2000,false,3,p);assert(!p.writes);for(unsigned t=0;t<500;t+=20)s.changed(t);s.tick(1479,false,1,p);assert(!p.writes);s.tick(1480,true,1,p);assert(!p.writes);s.tick(1480,false,1,p);assert(p.writes==1&&p.value==1&&!s.pending);s.changed(UINT32_MAX-50);s.tick(949,false,1,p);assert(!s.pending&&p.writes==1);std::cout<<"Bass Boost persistence: coalesce, debounce, blocked save, unchanged no-write and tick wrap: PASS\n";}
