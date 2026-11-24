"""Execute C3 track/state handlers: restart, metadata race, authoritative seek."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];s=(base/'display/src/main.cpp').read_text();code=s[s.index('void networkResume()'):s.index('void transportTick()')]
stub=r'''
#include "link_protocol.h"
#include "lyrics_core.h"
#include "player_controls.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <iostream>
uint32_t now=1000;uint32_t millis(){return now;}void uiDirty(){}
size_t strlcpy(char*d,const char*s,size_t n){size_t k=strlen(s);if(n){memcpy(d,s,std::min(k,n-1));d[std::min(k,n-1)]=0;}return k;}
struct Track{uint32_t generation=0,duration=0;char title[192]{},artist[128]{},album[128]{};}track;
std::atomic<uint32_t> generation{1};vinyl::PackedLyrics lyrics;vinyl::Clock playClock;vinyl::MemoryRetry memoryWait;
bool dirty=false,loadingLyrics=false,haveArtwork=false,linkOnline=false;uint32_t changedAt=0,retryAt=0;unsigned retryCount=0;char lyricStatus[40]{};
uint32_t remoteSession=0,remoteGeneration=0,lastSnapshot=0,lastAudio=0,volumeIntentAt=0;int desiredVolume=0,desiredBass=2;uint32_t toneIntentAt=0;bool inflightTone=false;bool toneInFlight(){return inflightTone;}unsigned first=0,count=0;linkwire::StateData audioState;
struct{bool active=false,received=false;uint16_t seq=0;uint8_t type=0,status=255;uint32_t offset=0;}relay;
struct{bool active=false;uint16_t seq=0;}pending;
unsigned snapshots=0;void command(uint8_t c){assert(c==linkwire::Snapshot);++snapshots;}
struct{template<class...T>void printf(const char*,T...){}}Serial;
'''
checks=r'''
int main(){linkwire::Frame hello;hello.type=linkwire::Hello;hello.len=5;hello.data[4]=0;linkwire::put32(hello.data,42);receiveFrame(hello);assert(remoteSession==42&&snapshots==1);
 linkwire::Frame meta;meta.type=linkwire::Track;meta.len=460;linkwire::put32(meta.data,42);linkwire::put32(meta.data+4,9);linkwire::put32(meta.data+8,180000);strcpy((char*)meta.data+12,"Bài Ca Tuổi Trẻ");strcpy((char*)meta.data+204,"Da LAB");receiveFrame(meta);assert(remoteGeneration==9&&track.title[0]&&dirty);
 auto gen=generation.load();receiveFrame(meta);assert(generation==gen);
 linkwire::StateData st;st.session=42;st.generation=9;st.position=123456;st.duration=180000;st.flags=linkwire::CONNECTED|linkwire::PLAYING|linkwire::AUTHORITATIVE;
 linkwire::Frame state;state.type=linkwire::State;state.len=25;linkwire::packState(state.data,st);receiveFrame(state);assert(linkOnline&&playClock.authoritative&&playClock.position(now)==123456);assert(playClock.position(now+500)==123956);
 st.position=25000;st.flags=linkwire::CONNECTED|linkwire::AUTHORITATIVE;linkwire::packState(state.data,st);receiveFrame(state);assert(!playClock.playing&&playClock.position(now+500)==25000);
 st.bass=3;linkwire::packState(state.data,st);state.len=linkwire::EXTENDED_STATE_BYTES;state.data[25]=12;receiveFrame(state);assert(desiredBass==3);
 toneIntentAt=now;desiredBass=1;receiveFrame(state);assert(desiredBass==1);now+=501;inflightTone=true;receiveFrame(state);assert(desiredBass==1);inflightTone=false;receiveFrame(state);assert(desiredBass==3);
 ++now;st.generation=10;linkwire::packState(state.data,st);receiveFrame(state);assert(!track.title[0]&&!playClock.authoritative&&generation!=gen);
 linkwire::put32(meta.data+4,10);receiveFrame(meta);assert(remoteGeneration==10&&track.title[0]);
 linkwire::put32(hello.data,43);receiveFrame(hello);assert(!track.title[0]&&!playClock.authoritative&&remoteGeneration==0);
 receiveFrame(meta);assert(!track.title[0]);receiveFrame(state);assert(!track.title[0]);
 std::cout<<"Actual C3 receiver: repeated metadata, authoritative playback/pause/seek, stale-generation clear, audio reboot nonce and stale-session rejection: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'receiver.cpp').write_text(stub+code+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'shared'),str(p/'receiver.cpp'),'-o',str(p/'receiver')],check=True);subprocess.run([str(p/'receiver')],check=True)
