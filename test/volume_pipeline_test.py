"""The real A2DP PCM callback + project volume adapter + EQ + mono packing."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];main=(base/'audio/src/main.cpp').read_text();sink=(base/'audio/lib/ESP32-A2DP/src/BluetoothA2DPSink.cpp').read_text()
adapter='std::atomic<uint32_t> volumeMaxUs{0};\n'+main[main.index('class SpeakerVolume'):main.index('BluetoothA2DPSinkQueued speaker;')]
callback=sink[sink.index('void BluetoothA2DPSink::audio_data_callback('):sink.index('bool BluetoothA2DPSink::is_avrc_connected()')]
stub=r'''
#include <cassert>
#include "bass_tone.h"
#include "A2DPVolumeControl.h"
#include "A2DPMonoQueue.h"
#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
uint32_t micros(){return 1;}
'''
harness=r'''
vinyl::BassTone eq;
bool rawSeen=false,eqSeen=false;unsigned frames=1024;std::vector<uint8_t> original,output;
void raw(const uint8_t*data,uint32_t n){assert(n==original.size()&&!memcmp(data,original.data(),n));rawSeen=true;}
void shaped(const uint8_t*data,uint32_t n){assert(rawSeen);eq.process(const_cast<uint8_t*>(data),n);eqSeen=true;}
struct BluetoothA2DPSink{
 bool swap_left_right=false,is_output=true;
 void(*raw_stream_reader)(const uint8_t*,uint32_t)=raw;
 void(*stream_reader)(const uint8_t*,uint32_t)=shaped;
 void(*data_received)()=nullptr;
 A2DPVolumeControl*volume_control(){return &speakerVolume;}
 void audio_data_callback(const uint8_t*,uint32_t);
 size_t write_audio(const uint8_t*p,size_t n){assert(eqSeen);size_t m=a2dp_mono::packLeftInPlace(const_cast<uint8_t*>(p),n);assert(m==n/2);output.assign(p,p+m);return n;}
};
'''
checks=r'''
int main(){
 BluetoothA2DPSink sink;vinyl::BassTone expectedEQ;vinyl::SmoothMonoVolume expectedVolume;
 for(unsigned preset=0;preset<4;++preset){eq.setLevel(preset);expectedEQ.setLevel(preset);
  for(unsigned vol:{0u,55u,127u,42u}){speakerVolume.set_volume(vol);expectedVolume.set(vol);
   for(unsigned block=0;block<6;++block){std::vector<uint8_t> bytes(4097);auto data=bytes.data()+1;
    for(unsigned i=0;i<4096;++i)data[i]=uint8_t(i*13+block*31);original.assign(data,data+4096);auto expected=original;
    expectedVolume.process(expected.data(),expected.size());expectedEQ.process(expected.data(),expected.size());a2dp_mono::packLeftInPlace(expected.data(),expected.size());expected.resize(expected.size()/2);
    rawSeen=eqSeen=false;sink.audio_data_callback(data,4096);assert(output==expected);
   }
  }
 }
 // Independently check the PCM510xA use case: either DAC channel must
 // contain the average, including music present only on the other channel.
 speakerVolume.set_volume(127);eq.setLevel(0);
 original.assign(4096,0);rawSeen=eqSeen=false;sink.audio_data_callback(original.data(),4096);
 for(unsigned rate:{44100u,48000u}){
  eq.setRate(rate);eq.reset();
  const int pairs[][2]={{16000,0},{0,16000},{32767,32767},{-32768,-32768},{20000,-20000},{-32768,32767}};
  for(const auto& pair:pairs){
   std::vector<uint8_t> block(4096);
   for(size_t at=0;at<block.size();at+=4){uint16_t l=uint16_t(int16_t(pair[0])),r=uint16_t(int16_t(pair[1]));block[at]=l&255;block[at+1]=l>>8;block[at+2]=r&255;block[at+3]=r>>8;}
   original=block;rawSeen=eqSeen=false;sink.audio_data_callback(block.data(),block.size());
   std::vector<uint8_t> stereo(4096);assert(a2dp_mono::expandStereo(output.data(),output.size(),stereo.data(),stereo.size())==stereo.size());
   int expected=(pair[0]+pair[1])/2;
   for(size_t at=0;at<stereo.size();at+=2)assert(int16_t(uint16_t(stereo[at])|(uint16_t(stereo[at+1])<<8))==expected);
  }
 }
 std::cout<<"Actual A2DP callback/volume adapter: raw -> smoothed mono volume -> EQ -> half-size queue, presets/remote volumes/unaligned buffers; PCM510xA L/R mono, L-only/R-only/extremes/cancellation at 44.1/48kHz: PASS\n";
}
'''
assert main.index('set_volume_control(&speakerVolume)')<main.index('speaker.set_mono_queue(true)')
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'esp_log.h').write_text('#pragma once\n#define ESP_LOGD(...)\n')
 header=(base/'audio/lib/ESP32-A2DP/src/A2DPVolumeControl.h').read_text()
 (p/'A2DPVolumeControl.h').write_text(header[:header.index('class A2DPDefaultVolumeControl')])
 (p/'pipeline.cpp').write_text(stub+adapter+harness+callback+checks)
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=address,undefined','-I'+str(p),'-I'+str(base/'shared'),'-I'+str(base/'audio/lib/ESP32-A2DP/src'),str(p/'pipeline.cpp'),'-o',str(p/'pipeline')],check=True)
 subprocess.run([str(p/'pipeline')],check=True)
