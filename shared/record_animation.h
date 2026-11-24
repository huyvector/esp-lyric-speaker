#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "artwork_render.h"
#include "vinyl_skin.h"
namespace vinyl {
constexpr int RECORD_RADIUS=56, RECORD_DIAMETER=112;
constexpr unsigned RECORD_TILE_ROWS=4, RECORD_ROWS_PER_TICK=12, RECORD_FRAME_MS=40;
// Small flash tables, no retained framebuffer. Lift midtones, preserve black/white.
constexpr uint8_t ROUND_LIFT_5[32]={0,2,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,23,24,25,26,27,28,29,29,30,31};
constexpr uint8_t ROUND_LIFT_6[64]={0,2,4,5,7,8,9,10,12,13,14,15,16,17,18,19,20,22,23,24,25,26,27,28,29,30,30,31,32,33,34,35,36,37,38,39,40,41,42,43,43,44,45,46,47,48,49,50,50,51,52,53,54,55,56,56,57,58,59,60,61,61,62,63};
constexpr uint8_t RECORD_CHORD[112]={0,10,14,18,20,23,25,27,28,30,31,33,34,35,37,38,39,40,41,42,42,43,44,45,45,46,47,47,48,49,49,50,50,51,51,51,52,52,53,53,53,53,54,54,54,54,55,55,55,55,55,55,55,55,55,55,56,55,55,55,55,55,55,55,55,55,55,54,54,54,54,53,53,53,53,52,52,51,51,51,50,50,49,49,48,47,47,46,45,45,44,43,42,42,41,40,39,38,37,35,34,33,31,30,28,27,25,23,20,18,14,10};
inline uint16_t roundLift(uint16_t c) {
 return uint16_t(unsigned(ROUND_LIFT_5[c>>11])<<11 | unsigned(ROUND_LIFT_6[(c>>5)&63])<<5 | ROUND_LIFT_5[c&31]);
}
inline unsigned recordHalfWidth(int y) {
 return y>=-RECORD_RADIUS && y<RECORD_RADIUS ? RECORD_CHORD[y+RECORD_RADIUS] : 0;
}
class RecordMotion {
 uint32_t stamp=0; bool initialized=false;
public:
 uint32_t phase=0;
 void tick(uint32_t now,bool playing) {
  if(!initialized){stamp=now;initialized=true;return;}
  unsigned elapsed=std::min(250u,unsigned(uint32_t(now-stamp)));stamp=now;
  // ~0.4 rad/s, matching the old nominal speed. Unsigned phase wraps naturally.
  if(playing)phase+=elapsed*273426u;
 }
 float angleRadians() const {return float(phase)*(6.2831853071795864769f/4294967296.0f);}
};
struct RecordTransform {
 int xx=0,xy=0,yx=0,yy=0,center=0;
 void set(int cosineQ14,int sineQ14,int size) {
  xx=cosineQ14*size*4/RECORD_DIAMETER;xy=sineQ14*size*4/RECORD_DIAMETER;
  yx=-xy;yy=xx;center=(size-1)*32768;
 }
 void rowStart(int x,int y,int& sx,int& sy) const {
  sx=center+x*xx+y*xy;sy=center+x*yx+y*yy;
 }
};
// Rows are packed contiguously for one SPI rectangle. Only the narrow fringe
// outside the album circle needs a static-record pixel; the image stays bilinear.
// Worst-case scratch is 112*4*2 = 896 bytes, reused for every tile.
inline bool recordTile(uint16_t* out,size_t capacity,int top,unsigned rows,
                       const RecordTransform& transform,const uint32_t* artwork,int size,
                       int& left,unsigned& width) {
 if(!out||!rows||rows>RECORD_TILE_ROWS||top<-RECORD_RADIUS||top+int(rows)>RECORD_RADIUS||size<1)return false;
 unsigned half=0;for(unsigned r=0;r<rows;++r)half=std::max(half,recordHalfWidth(top+int(r)));
 left=-int(half);width=half*2;
 if(capacity<size_t(width)*rows)return false;
 for(unsigned r=0;r<rows;++r){int y=top+int(r),sx,sy;transform.rowStart(left,y,sx,sy);
  unsigned chord=recordHalfWidth(y);
  for(unsigned column=0;column<width;++column){int x=left+int(column);uint16_t color;
   if(x<-int(chord)||x>=int(chord))color=recordPixel(x,y);
   else {
    color=artwork?sampleArtwork(artwork,size,sx/256,sy/256):uint16_t(0xc514);
    color=coverLight(color,x,y);int radius2=x*x+y*y;
    if(radius2<9)color=0x1082;if(radius2<2)color=0xdefb;
   }
   out[size_t(r)*width+column]=roundLift(color);sx+=transform.xx;sy+=transform.yx;
  }
 }
 return true;
}
}
