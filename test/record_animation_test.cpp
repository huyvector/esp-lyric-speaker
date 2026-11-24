#include "record_animation.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
int main(){
 vinyl::RecordMotion fast,slow;fast.tick(0,true);slow.tick(0,true);
 for(unsigned t=1;t<=1000;++t)fast.tick(t,true);
 for(unsigned t=100;t<=1000;t+=100)slow.tick(t,true);
 assert(fast.phase==slow.phase&&fast.phase==1000u*273426u);assert(std::abs(fast.angleRadians()-0.4f)<0.00001f);
 uint32_t frozen=fast.phase;fast.tick(1100,false);fast.tick(5000,false);assert(fast.phase==frozen);
 fast.tick(5100,true);assert(fast.phase-frozen==100u*273426u);
 frozen=fast.phase;fast.tick(1000000,true);assert(fast.phase-frozen==250u*273426u);
 vinyl::RecordMotion wrap;wrap.tick(UINT32_MAX-49,true);wrap.tick(50,true);assert(wrap.phase==100u*273426u);
 assert(vinyl::roundLift(0)==0&&vinyl::roundLift(0xffff)==0xffff);
 for(unsigned n=0;n<32;++n){assert(vinyl::ROUND_LIFT_5[n]>=n);if(n)assert(vinyl::ROUND_LIFT_5[n]>=vinyl::ROUND_LIFT_5[n-1]);}
 for(unsigned n=0;n<64;++n){assert(vinyl::ROUND_LIFT_6[n]>=n);if(n)assert(vinyl::ROUND_LIFT_6[n]>=vinyl::ROUND_LIFT_6[n-1]);}
 for(int y=-56;y<56;++y)assert(vinyl::recordHalfWidth(y)==unsigned(std::sqrt(double(56*56-y*y))));
 assert(!vinyl::recordHalfWidth(-57)&&!vinyl::recordHalfWidth(56));
 std::vector<uint32_t> art(96*96/2);
 for(int y=0;y<96;++y)for(int x=0;x<96;++x){size_t at=y*96+x;uint16_t color=vinyl::rgb(x*255/95,y*255/95,(x+y)*255/190);art[at/2]|=uint32_t(color)<<((at%2)*16);}
 uint16_t guarded[450];unsigned tiles=0;
 for(unsigned a=0;a<360;a+=13){int co=int(std::lround(std::cos(a*3.141592653589793/180)*16384)),si=int(std::lround(std::sin(a*3.141592653589793/180)*16384));vinyl::RecordTransform transform;transform.set(co,si,96);
  bool covered[112][112]{};
  for(int top=-56;top<56;top+=4){std::fill(std::begin(guarded),std::end(guarded),0xa55a);int left;unsigned width;
   assert(vinyl::recordTile(guarded+1,448,top,4,transform,art.data(),96,left,width));++tiles;
   assert(guarded[0]==0xa55a&&guarded[449]==0xa55a&&width<=112);
   for(int r=0;r<4;++r)for(unsigned col=0;col<width;++col){int x=left+int(col),y=top+r;bool inside=x>=-int(vinyl::recordHalfWidth(y))&&x<int(vinyl::recordHalfWidth(y));uint16_t ref;
    if(!inside)ref=vinyl::recordPixel(x,y);
    else {
     covered[y+56][x+56]=true;
     double sx=47.5+(double(x)*co+double(y)*si)/16384*96/112;
     double sy=47.5+(-double(x)*si+double(y)*co)/16384*96/112;
     ref=vinyl::sampleArtwork(art.data(),96,int(sx*256),int(sy*256));ref=vinyl::coverLight(ref,x,y);
     if(x*x+y*y<9)ref=0x1082;if(x*x+y*y<2)ref=0xdefb;
    }
    ref=vinyl::roundLift(ref);auto actual=guarded[1+r*width+col];
    assert(std::abs(int(actual>>11)-int(ref>>11))<=2);
    assert(std::abs(int((actual>>5)&63)-int((ref>>5)&63))<=2);
    assert(std::abs(int(actual&31)-int(ref&31))<=2);
   }
  }
  for(int y=-56;y<56;++y)for(int x=-56;x<56;++x)assert(covered[y+56][x+56]==(x>=-int(vinyl::recordHalfWidth(y))&&x<int(vinyl::recordHalfWidth(y))));
 }
 vinyl::RecordTransform t;t.set(16384,0,96);int left;unsigned width;std::fill(std::begin(guarded),std::end(guarded),0xa55a);
 assert(!vinyl::recordTile(guarded+1,1,0,4,t,art.data(),96,left,width));
 assert(!vinyl::recordTile(guarded+1,448,-57,4,t,art.data(),96,left,width));
 assert(!vinyl::recordTile(guarded+1,448,55,4,t,art.data(),96,left,width));
 assert(!vinyl::recordTile(guarded+1,448,0,5,t,art.data(),96,left,width));
 for(auto v:guarded)assert(v==0xa55a);
 assert(vinyl::recordTile(guarded+1,448,0,4,t,nullptr,96,left,width));
 std::cout<<"Record animation: cadence-independent phase, pause/resume/tick wrap/stall cap; "<<tiles<<" guarded 4-row tiles, full circular coverage/fringe restore, floating affine reference, bounded scratch, brightness endpoints/monotonicity: PASS\n";
}
