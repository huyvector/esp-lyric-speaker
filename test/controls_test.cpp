#include "player_controls.h"
#include "audio_feedback.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace vinyl;
int main(){
 Key k;assert(k.tick(false,0,true)==KeyAction::None);k.tick(true,10,true);k.tick(false,20,true);k.tick(true,25,true);assert(k.tick(true,55,true)==KeyAction::None);
 assert(k.tick(true,704,true)==KeyAction::None);assert(k.tick(true,705,true)==KeyAction::Hold);assert(k.tick(true,814,true)==KeyAction::None);assert(k.tick(true,815,true)==KeyAction::Repeat);k.tick(false,816,true);assert(k.tick(false,846,true)==KeyAction::None);
 k.tick(true,900,true);k.tick(true,930,true);k.tick(false,1000,true);assert(k.tick(false,1030,true)==KeyAction::Click);assert(k.tick(false,1100,true)==KeyAction::None);
 Key boot;boot.tick(true,0,true);boot.tick(true,31,true);assert(boot.tick(true,2000,true)==KeyAction::None);boot.tick(false,2001,true);assert(boot.tick(false,2031,true)==KeyAction::None);boot.tick(false,2032,true);boot.tick(true,2100,true);boot.tick(true,2130,true);assert(boot.tick(true,2780,true)==KeyAction::Hold);
 Key wrap;wrap.tick(false,UINT32_MAX-100,true);wrap.tick(true,UINT32_MAX-80,true);wrap.tick(true,UINT32_MAX-50,true);assert(wrap.tick(true,600,true)==KeyAction::Hold);
 MemoryRetry retry;retry.arm(9,0);for(unsigned i=0;i<100000;i+=100)assert(!retry.ready(9,i,42000,18000,true));assert(retry.pending);assert(!retry.ready(9,100000,45000,21000,true));assert(!retry.ready(9,100900,45000,21000,false));assert(!retry.ready(9,101000,45000,21000,true));assert(retry.ready(9,102000,45000,21000,true));assert(!retry.pending);retry.arm(9,0);assert(!retry.ready(10,5000,50000,30000,true)&&!retry.pending);
 retry.arm(10,UINT32_MAX-2000);assert(!retry.ready(10,999,50000,30000,true));assert(!retry.ready(10,1000,50000,30000,true));assert(retry.ready(10,2000,50000,30000,true));
 VolumePopup popup;popup.show(20,40,false,1,100);int mid=popup.valueQ8(170);assert(mid>20*256&&mid<40*256);popup.show(40,60,false,1,170);assert(popup.valueQ8(170)==mid);assert(popup.valueQ8(310)==60*256);assert(!popup.tick(1469));assert(popup.tick(1470));assert(!popup.visible);
 assert(useVU(OLEDMode::Auto,false)&&!useVU(OLEDMode::Auto,true)&&useVU(OLEDMode::VU,true)&&!useVU(OLEDMode::Lyric,false));
 for(unsigned hz:{8000,16000,32000,44100,48000}){
  AudioFeedback f;f.setRate(hz);uint8_t block[1024]{};f.process(block,sizeof block);for(auto c:block)assert(!c);f.beep();assert(f.pending());unsigned frames=0;bool heard=false;
  while(f.pending()){memset(block,0,sizeof block);f.process(block,sizeof block);for(unsigned i=0;i<sizeof block;i+=4){int x=int16_t(uint16_t(block[i])|uint16_t(block[i+1])<<8);assert(x>=-900&&x<=900);assert(block[i]==block[i+2]&&block[i+1]==block[i+3]);heard|=x!=0;}frames+=256;assert(frames<hz/5);}
  assert(heard&&frames>=hz*70/1000);assert(f.takePeak()==0);memset(block,0,sizeof block);f.process(block,sizeof block);for(auto c:block)assert(!c);
  for(unsigned i=0;i<sizeof block;i+=4){block[i]=block[i+2]=0;block[i+1]=block[i+3]=128;}f.process(block,sizeof block);assert(f.takePeak()==32768&&f.takePeak()==0);
 }
 std::cout<<"Controls: debounce, short/hold/repeat, boot-held, rollover, indefinite memory wait, stable recovery, popup retarget, OLED modes; PCM feedback finite/enveloped at five rates: PASS\n";
}
