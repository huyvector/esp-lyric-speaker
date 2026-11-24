#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
namespace vinyl {
enum class VisualMode : uint8_t { Auto, Ribbons, Orbit, Stars, Waves };
constexpr unsigned VISUAL_COUNT=5, MAIN_MENU_COUNT=9, VISUAL_MENU_COUNT=6;
constexpr int VISUAL_TOP=18, VISUAL_BOTTOM=71, VISUAL_WIDTH=284;
constexpr int VISUAL_HEIGHT=VISUAL_BOTTOM-VISUAL_TOP+1,VISUAL_CENTER=(VISUAL_TOP+VISUAL_BOTTOM+1)/2;
// Own fixed-point animations, driven by the existing peak telemetry. These
// are decorative visualizations, not an FFT or a sampled audio waveform.
constexpr int16_t VISUAL_SINE[256]={0,6,13,19,25,31,38,44,50,56,62,68,74,80,86,92,98,104,109,115,121,126,132,137,142,147,152,157,162,167,172,177,181,185,190,194,198,202,206,209,213,216,220,223,226,229,231,234,237,239,241,243,245,247,248,250,251,252,253,254,255,255,256,256,256,256,256,255,255,254,253,252,251,250,248,247,245,243,241,239,237,234,231,229,226,223,220,216,213,209,206,202,198,194,190,185,181,177,172,167,162,157,152,147,142,137,132,126,121,115,109,104,98,92,86,80,74,68,62,56,50,44,38,31,25,19,13,6,0,-6,-13,-19,-25,-31,-38,-44,-50,-56,-62,-68,-74,-80,-86,-92,-98,-104,-109,-115,-121,-126,-132,-137,-142,-147,-152,-157,-162,-167,-172,-177,-181,-185,-190,-194,-198,-202,-206,-209,-213,-216,-220,-223,-226,-229,-231,-234,-237,-239,-241,-243,-245,-247,-248,-250,-251,-252,-253,-254,-255,-255,-256,-256,-256,-256,-256,-255,-255,-254,-253,-252,-251,-250,-248,-247,-245,-243,-241,-239,-237,-234,-231,-229,-226,-223,-220,-216,-213,-209,-206,-202,-198,-194,-190,-185,-181,-177,-172,-167,-162,-157,-152,-147,-142,-137,-132,-126,-121,-115,-109,-104,-98,-92,-86,-80,-74,-68,-62,-56,-50,-44,-38,-31,-25,-19,-13,-6};
inline int visualSin(unsigned phase){return VISUAL_SINE[phase&255];}
struct VisualScene {
 struct Star {int16_t x,y,dx,dy;uint8_t brightness;};
 int16_t columns[3][284]{},rowTerms[3][VISUAL_HEIGHT]{};Star stars[28]{};
 uint32_t animationTime=0,stamp=0;unsigned level=0,style=0;bool initialized=false;
 void prepare(VisualMode mode,uint32_t now,unsigned peak,bool playing){
  unsigned dt=initialized?std::min(250u,unsigned(now-stamp)):0;stamp=now;initialized=true;
  if(playing)animationTime+=dt;
  unsigned target=playing?std::min(256u,peak*256/20000):0;
  unsigned tau=target>level?45:180;level=(level*tau+target*dt)/(tau+dt);
  style=mode==VisualMode::Auto?(animationTime/16000)%4:unsigned(mode)-1;
  unsigned phase=animationTime/18;int amplitude=4+level*15/256;
  for(unsigned layer=0;layer<3;++layer){
   int cx=142+visualSin(phase/3+layer*79)*20/256;
   int rx=88+int(layer)*17,ry=10+int(layer)*5,cy=VISUAL_CENTER+visualSin(phase/2+layer*70)*3/256;
   for(int x=0;x<284;++x){
    if(style==1)columns[layer][x]=(x-cx)*(x-cx)*256/(rx*rx);
    else {int wave=visualSin(x*(layer+1)/2+phase*(layer+1)+layer*77);int ripple=visualSin(x*2-phase+layer*53);
     int displacement=style==3?(wave*amplitude*(x<142?x:283-x)/142+ripple*2):(wave*amplitude+ripple*2);
     columns[layer][x]=(VISUAL_CENTER+int(layer)*2-2)*256+displacement;
    }
   }
   for(int y=VISUAL_TOP;y<=VISUAL_BOTTOM;++y)rowTerms[layer][y-VISUAL_TOP]=(y-cy)*(y-cy)*256/(ry*ry);
  }
  for(unsigned i=0;i<28;++i){unsigned seed=i*1664525u+1013904223u;int vx=int((seed>>16)%271)-135;int vy=int((seed>>8)%35)-17;
   unsigned depth=(animationTime/(10-level/32)+(seed&1023))&1023;unsigned distance=depth*depth/1024;
   stars[i]={int16_t(142+vx*int(distance)/512),int16_t(VISUAL_CENTER+vy*int(distance)/512),int16_t(vx>0?1:-1),int16_t(vy>0?1:-1),uint8_t(12+depth*35/1024)};
  }
 }
 unsigned pixel(int x,int y)const{
  unsigned color=std::min(15,y/5),brightest=0;
  if(style==2)return color;
  constexpr unsigned ramps[3]={160,208,64};
  for(unsigned layer=0;layer<3;++layer){
   int distance=style==1?std::abs(int(columns[layer][x])+int(rowTerms[layer][y-VISUAL_TOP])-256)*14:std::abs((y<<8)-int(columns[layer][x]));
   unsigned radius=style==1?950:1100;
   if(unsigned(distance)<radius){unsigned strength=(radius-unsigned(distance))*47/radius;strength=strength*(165+level/3)/256;
    if(strength>brightest){brightest=strength;color=ramps[layer]+strength;}
   }
  }
  return color;
 }
 void row(int y,uint8_t* out)const{
  for(int x=0;x<284;++x)out[x]=pixel(x,y);
  if(style!=2)return;
  for(const auto& s:stars)for(int trail=0;trail<5;++trail){int yy=s.y-s.dy*trail,xx=s.x-s.dx*trail*2;if(std::abs(y-yy)>1||xx<1||xx>=283)continue;
   unsigned brightness=s.brightness*(5-trail)/5;out[xx]=uint8_t((trail?160:16)+brightness);if(y==yy){out[xx-1]=uint8_t(160+brightness/2);out[xx+1]=uint8_t(160+brightness/2);}
  }
 }
};
}
