#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
namespace vinyl {
// Constant waveform in flash. No float, allocation, or second I2S writer.
constexpr int16_t DIAGNOSTIC_SINE[256]={
 0,804,1608,2410,3212,4011,4808,5602,6393,7179,7962,8739,9512,10278,11039,11793,
 12539,13279,14010,14732,15446,16151,16846,17530,18204,18868,19519,20159,20787,21403,22005,22594,
 23170,23731,24279,24811,25329,25832,26319,26790,27245,27683,28105,28510,28898,29268,29621,29956,
 30273,30571,30852,31113,31356,31580,31785,31971,32137,32285,32412,32521,32609,32678,32728,32757,
 32767,32757,32728,32678,32609,32521,32412,32285,32137,31971,31785,31580,31356,31113,30852,30571,
 30273,29956,29621,29268,28898,28510,28105,27683,27245,26790,26319,25832,25329,24811,24279,23731,
 23170,22594,22005,21403,20787,20159,19519,18868,18204,17530,16846,16151,15446,14732,14010,13279,
 12539,11793,11039,10278,9512,8739,7962,7179,6393,5602,4808,4011,3212,2410,1608,804,
 0,-804,-1608,-2410,-3212,-4011,-4808,-5602,-6393,-7179,-7962,-8739,-9512,-10278,-11039,-11793,
 -12539,-13279,-14010,-14732,-15446,-16151,-16846,-17530,-18204,-18868,-19519,-20159,-20787,-21403,-22005,-22594,
 -23170,-23731,-24279,-24811,-25329,-25832,-26319,-26790,-27245,-27683,-28105,-28510,-28898,-29268,-29621,-29956,
 -30273,-30571,-30852,-31113,-31356,-31580,-31785,-31971,-32137,-32285,-32412,-32521,-32609,-32678,-32728,-32757,
 -32767,-32757,-32728,-32678,-32609,-32521,-32412,-32285,-32137,-31971,-31785,-31580,-31356,-31113,-30852,-30571,
 -30273,-29956,-29621,-29268,-28898,-28510,-28105,-27683,-27245,-26790,-26319,-25832,-25329,-24811,-24279,-23731,
 -23170,-22594,-22005,-21403,-20787,-20159,-19519,-18868,-18204,-17530,-16846,-16151,-15446,-14732,-14010,-13279,
 -12539,-11793,-11039,-10278,-9512,-8739,-7962,-7179,-6393,-5602,-4808,-4011,-3212,-2410,-1608,-804,
};
class DiagnosticTone {
 std::atomic<unsigned> requested{0}; // 0 none, 1 stop, frequency+2 start
 std::atomic<bool> live{false};
 uint32_t phase=0,step=0,left=0,total=0;unsigned previousRate=0,ramp=1;
public:
 bool start(unsigned frequency){if(frequency<100||frequency>2000)return false;requested.store(frequency+2);return true;}
 void stop(){requested.store(1);}
 bool active()const{unsigned r=requested.load();return r==1?false:r>=2||live.load();}
 bool pending()const{return requested.load()!=0||left!=0;} // Output task only.
 void begin(unsigned rate){
  if(rate!=previousRate){previousRate=rate;left=0;live.store(false);}
  unsigned request=requested.exchange(0);
  if(request==1){left=0;live.store(false);return;}
  if(request>=2&&rate>=8000){phase=0;step=uint32_t((uint64_t(request-2)<<32)/rate);total=left=rate*3;ramp=std::max(1u,rate/100);live.store(true);}
 }
 bool sample(int&out){if(!left)return false;
  unsigned index=phase>>24,fraction=(phase>>16)&255;
  int wave=DIAGNOSTIC_SINE[index]+(int(DIAGNOSTIC_SINE[(index+1)&255])-DIAGNOSTIC_SINE[index])*int(fraction)/256;
  unsigned edge=std::min(left,total-left),envelope=std::min(edge,ramp);
  out=int(int64_t(wave)*4096*envelope/(int64_t(32767)*ramp));phase+=step;
  if(!--left){live.store(false);}
  return true;
 }
};
}
