"""Execute actual audio command/UART-OTA receiver with deterministic transport."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];source=(base/'audio/src/main.cpp').read_text();code=source[source.index('void receive(){'):source.index('void health()')]
stub=r'''
#include "link_protocol.h"
#include "lyrics_core.h"
#include <vector>
#include <deque>
#include <cassert>
#include <iostream>
uint32_t now=1000;uint32_t millis(){return now;}int constrain(int x,int a,int b){return std::max(a,std::min(b,x));}
struct {unsigned nextCalls=0,stopCalls=0,playCalls=0,pauseCalls=0;int volume=0;void next(){++nextCalls;}void previous(){}void play(){++playCalls;}void pause(){++pauseCalls;}void set_volume(int n){volume=n;}void end(bool){++stopCalls;}}speaker;
struct{int low=2;void setLevel(int v){low=v;}unsigned level(){return low;}}bassTone;struct{unsigned writes=0;void changed(uint32_t){++writes;}}toneStore;struct{void putUInt(const char*,unsigned){}}prefs;struct{void beep(){}}feedback;struct{void println(const char*){}}Serial;
struct ImageUpdate{size_t at=0;bool live=false;unsigned commits=0;static bool valid(size_t s,const char*m){if(s<24||s>0x180000||strlen(m)!=32)return false;for(unsigned i=0;i<32;++i)if(!((m[i]>='0'&&m[i]<='9')||(m[i]>='a'&&m[i]<='f')))return false;return true;}bool begin(size_t,const char*,int){at=0;live=true;return true;}bool write(const uint8_t*,size_t n){at+=n;return live;}size_t offset(){return at;}bool finish(){if(!live)return false;live=false;++commits;return true;}void abort(){live=false;}}updater;
uint32_t peer=0,otaAt=0,rebootAt=0;bool dirty=false,updating=false;linkwire::Duplicate commands;uint8_t commandStatus=0;bool otaDone=false;uint32_t lastChunkAt=0;uint16_t lastChunkLen=0,lastChunkCRC=0;vinyl::Clock playClock;
struct{std::deque<linkwire::Frame>incoming;linkwire::Frame frame;bool receive(){if(incoming.empty())return false;frame=incoming.front();incoming.pop_front();return true;}}wire;
unsigned acks=0;uint8_t status=255;uint32_t offset=0;void ack(const linkwire::Frame&,uint8_t s,uint32_t o=0){++acks;status=s;offset=o;}void sendHello(){}void sendTrack(){}
void send(linkwire::Frame f){wire.incoming.push_back(f);}void receive();
linkwire::Frame frame(uint8_t type,uint16_t seq,uint16_t n){linkwire::Frame f;f.type=type;f.seq=seq;f.len=n;return f;}
'''
checks=r'''
int main(){auto h=frame(linkwire::Hello,1,5);linkwire::put32(h.data,99);h.data[4]=1;send(h);receive();assert(peer==99);
 auto c=frame(linkwire::Command,2,9);linkwire::put32(c.data,99);c.data[4]=linkwire::Next;send(c);receive();assert(!status&&speaker.nextCalls==1);send(c);receive();assert(!status&&speaker.nextCalls==1);
 ++c.seq;c.data[4]=255;send(c);receive();assert(status==2);send(c);receive();assert(status==2);
 ++c.seq;c.data[4]=linkwire::Next;linkwire::put32(c.data,100);send(c);receive();assert(status==1&&speaker.nextCalls==1);
 ++c.seq;linkwire::put32(c.data,99);c.data[4]=10;linkwire::put32(c.data+5,6);send(c);receive();assert(status==2&&toneStore.writes==0);
 ++c.seq;c.data[4]=linkwire::Bass;linkwire::put32(c.data+5,3);send(c);receive();assert(!status&&bassTone.level()==3&&toneStore.writes==1);send(c);receive();assert(toneStore.writes==1);
 auto begin=frame(linkwire::OtaBegin,10,37);begin.data[0]=0;linkwire::put32(begin.data+1,1000);memset(begin.data+5,'z',32);send(begin);receive();assert(status==3&&!updating&&!speaker.stopCalls);
 memset(begin.data+5,'a',32);send(begin);receive();assert(!status&&updating&&speaker.stopCalls==1);
 auto data=frame(linkwire::OtaData,11,260);linkwire::put32(data.data,0);memset(data.data+4,7,256);send(data);receive();assert(!status&&offset==256);send(data);receive();assert(!status&&offset==256);
 data.data[10]^=1;send(data);receive();assert(status==4&&offset==256);data.data[10]^=1;
 linkwire::put32(data.data,400);send(data);receive();assert(status==4&&offset==256);
 auto end=frame(linkwire::OtaEnd,12,0);send(end);receive();assert(!status&&updater.commits==1);send(end);receive();assert(!status&&updater.commits==1);
 std::cout<<"Actual audio receiver: wrong session/duplicate next/rejected-command retry, validate before audio stop, duplicate/mismatched/out-of-order OTA chunks, lost completion ACK: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'receiver.cpp').write_text(stub+code+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'shared'),str(p/'receiver.cpp'),'-o',str(p/'receiver')],check=True);subprocess.run([str(p/'receiver')],check=True)
