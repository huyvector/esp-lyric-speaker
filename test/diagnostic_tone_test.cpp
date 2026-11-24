#include "diagnostic_tone.h"
#include "audio_feedback.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
int sample(const uint8_t*p){return int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);}
int main(){
 for(unsigned rate:{8000u,16000u,32000u,44100u,48000u,96000u})for(unsigned frequency:{200u,1000u}){
  vinyl::DiagnosticTone tone;int value=4321;assert(!tone.pending()&&!tone.active()&&!tone.sample(value)&&value==4321);
  assert(!tone.start(0)&&!tone.start(9000));assert(tone.start(frequency)&&tone.active());tone.begin(rate);unsigned frames=0,peak=0;double error=0;unsigned measured=0;
  while(tone.sample(value)){assert(std::abs(value)<=4096);peak=std::max(peak,unsigned(std::abs(value)));
   if(frames>rate/50&&frames<rate*2){double expected=4096*sin(6.283185307179586*frequency*frames/rate);error+=(value-expected)*(value-expected);++measured;}++frames;
  }
  assert(frames==rate*3&&peak>4000&&!tone.active()&&!tone.pending());double rmsError=sqrt(error/measured);assert(rmsError<2.0);
  assert(tone.start(1000));tone.begin(rate);assert(tone.sample(value));tone.stop();tone.begin(rate);assert(!tone.sample(value)&&!tone.active()&&!tone.pending());
  // New sample-rate notification cancels an active tone, avoiding wrong pitch.
  assert(tone.start(1000));tone.begin(rate);tone.begin(rate+1000);assert(!tone.sample(value));
 }
 vinyl::AudioFeedback feedback;uint8_t bytes[1024];for(unsigned i=0;i<sizeof bytes;i+=4){bytes[i]=bytes[i+2]=0x39;bytes[i+1]=bytes[i+3]=0x30;}uint8_t copy[1024];memcpy(copy,bytes,sizeof bytes);feedback.process(bytes,sizeof bytes);assert(!memcmp(bytes,copy,sizeof bytes));
 assert(feedback.testTone(1000));unsigned frames=0;while(feedback.pending()){memset(bytes,0,sizeof bytes);feedback.process(bytes,sizeof bytes);for(unsigned i=0;i<sizeof bytes;i+=4){assert(std::abs(sample(bytes+i))<=4096&&sample(bytes+i)==sample(bytes+i+2));}frames+=256;assert(frames<44100*4);}assert(feedback.takePeak()>4000);
 memset(bytes,0,sizeof bytes);feedback.process(bytes,sizeof bytes);for(auto b:bytes)assert(!b);
 std::cout<<"Sine diagnostic: 6 rates/2 frequencies, known level, <2 LSB RMS waveform error, exact duration, finite ramp, cancellation/rate change; same output filter/music bypass: PASS\n";
}
