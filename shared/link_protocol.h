#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
namespace linkwire {
constexpr uint8_t VERSION=1;constexpr size_t MAX_PAYLOAD=512;
enum Type:uint8_t {Hello=1,Command=2,Ack=3,Track=4,State=5,OtaBegin=32,OtaData=33,OtaEnd=34,OtaAbort=35};
enum Cmd:uint8_t {Snapshot=1,PlayPause=2,Next=3,Previous=4,Volume=5,Bass=6,Cue=7,Play=8,Pause=9};
inline uint16_t get16(const uint8_t*p){return p[0]|uint16_t(p[1])<<8;}
inline uint32_t get32(const uint8_t*p){return get16(p)|uint32_t(get16(p+2))<<16;}
inline void put16(uint8_t*p,uint16_t v){p[0]=v;p[1]=v>>8;}
inline void put32(uint8_t*p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
inline uint16_t crc(const uint8_t*p,size_t n){uint16_t c=0xffff;while(n--){c^=uint16_t(*p++)<<8;for(unsigned i=0;i<8;++i)c=c&0x8000?(c<<1)^0x1021:c<<1;}return c;}
struct Frame {uint8_t type=0;uint16_t seq=0,len=0;uint8_t data[MAX_PAYLOAD]{};};
inline size_t encode(uint8_t*out,size_t cap,uint8_t type,uint16_t seq,const uint8_t*data,size_t len){if(len>MAX_PAYLOAD||cap<len+10||(!data&&len))return 0;out[0]=0xa5;out[1]=0x5a;out[2]=VERSION;out[3]=type;put16(out+4,seq);put16(out+6,len);if(len)memcpy(out+8,data,len);put16(out+8+len,crc(out+2,6+len));return len+10;}
class Parser {
 uint8_t bytes[MAX_PAYLOAD+10]{};size_t used=0,want=0;uint32_t last=0;
public:
 uint32_t errors=0,packets=0;
 bool feed(uint8_t b,uint32_t now,Frame&out){if(used&&uint32_t(now-last)>150){used=want=0;++errors;}last=now;
 if(!used){if(b==0xa5)bytes[used++]=b;return false;}
 if(used==1){if(b==0x5a)bytes[used++]=b;else used=b==0xa5?1:0;return false;}
 bytes[used++]=b;
 if(used==8){size_t n=get16(bytes+6);if(bytes[2]!=VERSION||n>MAX_PAYLOAD){used=want=0;++errors;return false;}want=n+10;}
 if(want&&used==want){bool good=get16(bytes+want-2)==crc(bytes+2,want-4);if(good){out.type=bytes[3];out.seq=get16(bytes+4);out.len=get16(bytes+6);if(out.len)memcpy(out.data,bytes+8,out.len);++packets;}else ++errors;used=want=0;return good;}return false;
 }
};
struct StateData {uint32_t session=0,generation=0,position=0,duration=0;uint8_t flags=0,volume=55,bass=2;uint16_t peak=0;uint32_t heap=0;};
constexpr unsigned STATE_BYTES=25,EXTENDED_STATE_BYTES=26;
constexpr uint8_t CONNECTED=1,PLAYING=2,AUTHORITATIVE=4,UPDATING=8;
inline void packState(uint8_t*p,const StateData&s){put32(p,s.session);put32(p+4,s.generation);put32(p+8,s.position);put32(p+12,s.duration);p[16]=s.flags;p[17]=s.volume;p[18]=s.bass;put16(p+19,s.peak);put32(p+21,s.heap);}
inline bool unpackState(const Frame&f,StateData&s){if(f.len!=STATE_BYTES&&f.len!=EXTENDED_STATE_BYTES)return false;s.session=get32(f.data);s.generation=get32(f.data+4);s.position=get32(f.data+8);s.duration=get32(f.data+12);s.flags=f.data[16];s.volume=std::min(uint8_t(127),f.data[17]);s.bass=std::min(uint8_t(3),f.data[18]);s.peak=get16(f.data+19);s.heap=get32(f.data+21);return true;}
struct Duplicate {uint32_t session=0;uint16_t seq=0;uint8_t type=0;bool valid=false;bool seen(uint32_t id,const Frame&f){if(valid&&session==id&&seq==f.seq&&type==f.type)return true;session=id;seq=f.seq;type=f.type;valid=true;return false;}};
}
