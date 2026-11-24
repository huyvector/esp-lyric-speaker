#include "bass_tone.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>
#include <cstring>
double response(unsigned rate,unsigned level,double hz,int amplitude=8000){
 vinyl::BassTone tone;tone.setRate(rate);tone.setLevel(level);std::vector<uint8_t> data(1024);double input=0,output=0;
 for(unsigned at=0;at<rate*2;){unsigned n=std::min(256u,rate*2-at);
  for(unsigned i=0;i<n;++i){int16_t x=int16_t(amplitude*sin(6.28318530718*hz*(at+i)/rate));data[i*4]=data[i*4+2]=uint16_t(x)&255;data[i*4+1]=data[i*4+3]=uint16_t(x)>>8;if(at+i>=rate)input+=double(x)*x;}
  tone.process(data.data(),n*4);
  for(unsigned i=0;i<n;++i){int16_t y=int16_t(uint16_t(data[i*4])|uint16_t(data[i*4+1])<<8);assert(data[i*4]==data[i*4+2]&&data[i*4+1]==data[i*4+3]);if(at+i>=rate)output+=double(y)*y;}at+=n;
 }
 return 10*log10(output/input);
}
int16_t sample(const uint8_t*b){return int16_t(uint16_t(b[0])|uint16_t(b[1])<<8);}
void set(uint8_t*b,int16_t x){b[0]=b[2]=uint16_t(x)&255;b[1]=b[3]=uint16_t(x)>>8;}
int main(){
 for(unsigned rate:vinyl::EQ_RATES){
  for(unsigned level=0;level<=3;++level){
   double low=response(rate,level,30),bass=response(rate,level,200),mid=response(rate,level,1000),hi=response(rate,level,std::min(6000u,rate*7/16));
   std::cout<<rate<<" preset="<<level<<" low="<<low<<" bass="<<bass<<" mid="<<mid<<" high="<<hi<<" dB\n";
   if(!level){assert(std::abs(bass)<.001&&std::abs(mid)<.001);continue;}
   assert(low < -10);assert(bass>mid+.9);assert(mid>hi+.8);
   if(level==2&&(rate==44100||rate==48000)){assert(bass>1.7&&bass<2.1);assert(hi< -3.3);}
  }
  vinyl::BassTone tone;tone.setRate(rate);tone.setLevel(3);uint8_t block[4097]{};uint32_t seed=31;
  for(unsigned n=0;n<900;++n){for(unsigned at=1;at<4097;at+=4){seed=seed*1664525u+1013904223u;set(block+at,n%3==0?-32768:n%3==1?32767:int16_t(seed>>16));}
   tone.process(block+1,4096);for(unsigned at=1;at<4097;at+=4)assert(std::abs(int(sample(block+at)))<=32768);
   if(n%31==0)tone.setLevel(n%4);if(n%97==0)tone.reset();
  }
  // Filters recover to near silence after full-scale stress; no runaway/DC latch.
  std::memset(block,0,sizeof block);for(unsigned n=0;n<200;++n)tone.process(block,4096);
  for(unsigned at=0;at<4096;at+=4)assert(std::abs(int(sample(block+at)))<=2);
 }
 // Repeated same settings/status snapshots do not create gain modulation.
 vinyl::BassTone steady;uint8_t cycle[1600];double minRms=1e30,maxRms=0;
 for(unsigned block=0;block<150;++block){steady.setRate(44100);steady.setLevel(2);
  for(unsigned i=0;i<400;++i)set(cycle+i*4,int16_t(10000*sin(6.28318530718*i/100)));
  steady.process(cycle,sizeof cycle);steady.takeStats();double power=0;
  for(unsigned i=0;i<400;++i){double v=sample(cycle+i*4);power+=v*v;}
  if(block>60){minRms=std::min(minRms,power);maxRms=std::max(maxRms,power);}
 }assert(maxRms/minRms<1.002);
 vinyl::BassTone knee;knee.setLevel(3);uint8_t loud[4096];for(unsigned block=0;block<100;++block){
  for(unsigned i=0;i<1024;++i)set(loud+i*4,int16_t(32767*sin(6.28318530718*(block*1024+i)*180/44100)));
  knee.process(loud,sizeof loud);
 }auto stats=knee.takeStats();assert(stats.limitedSamples>0&&stats.outputPeak<32000);
 vinyl::BassTone flat;flat.setLevel(0);uint8_t pcm[4096];for(unsigned i=0;i<1024;++i)set(pcm+i*4,int16_t(i*7919));uint8_t copy[4096];memcpy(copy,pcm,sizeof pcm);flat.process(pcm,sizeof pcm);assert(!memcmp(copy,pcm,sizeof pcm));
 flat.setRate(12345);flat.setLevel(2);flat.reset();flat.process(pcm,sizeof pcm);assert(!memcmp(copy,pcm,sizeof pcm));
 vinyl::SmoothMonoVolume volume;volume.set(127);for(unsigned i=0;i<1024;++i)set(pcm+i*4,20000);volume.process(pcm,sizeof pcm);assert(sample(pcm+4092)==20000);
 volume.set(55);assert(volume.targetQ16()>12200&&volume.targetQ16()<12300);for(unsigned i=0;i<1024;++i)set(pcm+i*4,20000);volume.process(pcm,sizeof pcm);assert(sample(pcm+4092)>3740&&sample(pcm+4092)<3760);
 // Once target is held, gain is exactly constant over arbitrary block boundaries.
 for(unsigned n=0;n<30;++n){for(unsigned i=0;i<1024;++i)set(pcm+i*4,20000);volume.process(pcm,sizeof pcm);for(unsigned i=0;i<1024;++i)assert(sample(pcm+i*4)==sample(pcm+4092));}
 volume.set(0);for(unsigned n=0;n<3;++n){for(unsigned i=0;i<1024;++i)set(pcm+i*4,20000);volume.process(pcm,sizeof pcm);}assert(sample(pcm+4092)==0);
 volume.set(127);for(unsigned i=0;i<1024;++i){pcm[i*4]=0xff;pcm[i*4+1]=0x7f;pcm[i*4+2]=0;pcm[i*4+3]=0x80;}volume.process(pcm,sizeof pcm);assert(sample(pcm+4092)==0);
 std::cout<<"Actual EQ: response/stability/extremes/unaligned/control/reset/flat/unsupported rate; volume unity/mute/downmix/settled-no-modulation: PASS\n";
}
