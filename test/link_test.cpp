#include "link_protocol.h"
#include "encoder_core.h"
#include <cassert>
#include <vector>
#include <iostream>
using namespace linkwire;
int main(){
 uint8_t bytes[MAX_PAYLOAD+10],body[MAX_PAYLOAD];for(unsigned i=0;i<sizeof body;++i)body[i]=uint8_t(i*37);
 Parser p;Frame f;unsigned accepted=0;uint32_t now=UINT32_MAX-300;
 for(unsigned n:{0u,1u,25u,460u,512u}){auto size=encode(bytes,sizeof bytes,Track,65535,body,n);assert(size==n+10);for(unsigned i=0;i<size;++i)if(p.feed(bytes[i],now++,f))++accepted;assert(f.len==n&&f.seq==65535&&f.type==Track&&!memcmp(f.data,body,n));}
 assert(accepted==5&&!p.errors);assert(!encode(bytes,12,Track,1,body,512));assert(!encode(bytes,sizeof bytes,Track,1,body,513));
 auto size=encode(bytes,sizeof bytes,State,7,body,25);bytes[11]^=1;for(unsigned i=0;i<size;++i)assert(!p.feed(bytes[i],now++,f));assert(p.errors==1);
 // Resynchronise after noise, partial packets, oversized/version-invalid headers, and timeout.
 p.feed(0xa5,now++,f);p.feed(0xa5,now++,f);now+=200;
 uint8_t bad[]={0xa5,0x5a,1,4,1,0,1,3};for(auto c:bad)assert(!p.feed(c,now++,f));
 size=encode(bytes,sizeof bytes,State,8,body,25);for(unsigned i=0;i<size;++i)if(p.feed(bytes[i],now++,f))++accepted;assert(accepted==6&&p.errors==3);
 StateData a;a.session=0xabcddcba;a.generation=123;a.position=456789;a.duration=999999;a.flags=CONNECTED|PLAYING|AUTHORITATIVE;a.volume=127;a.bass=3;a.peak=32767;a.heap=198765;
 packState(f.data,a);f.len=25;StateData b;assert(unpackState(f,b));assert(b.session==a.session&&b.position==a.position&&b.generation==123&&b.peak==32767&&b.heap==198765&&b.flags==7);f.len=24;assert(!unpackState(f,b));
 // New state is25bytes; old26byte input is accepted without its removed trim.
 packState(f.data,a);f.data[25]=12;f.len=EXTENDED_STATE_BYTES;assert(unpackState(f,b)&&b.bass==3&&b.position==a.position);f.len=27;assert(!unpackState(f,b));
 struct{uint8_t before=0x55,data[25]{},after=0xaa;}guard;packState(guard.data,a);assert(guard.before==0x55&&guard.after==0xaa);
 Duplicate d;f.type=Command;f.seq=9;assert(!d.seen(1,f)&&d.seen(1,f));assert(!d.seen(2,f));++f.seq;assert(!d.seen(2,f));
 vinyl::Rotary r;int total=0;for(int v:{2,0,1,3})total+=r.update(v);assert(total==-1);for(int v:{1,0,2,3})total+=r.update(v);assert(total==0);
 for(int i=0;i<30;++i){total+=r.update(2);total+=r.update(3);}assert(total==0);r.update(0);r.update(3);assert(r.partial==0);for(int v:{2,0,1,3})total+=r.update(v);assert(total==-1);
 std::cout<<"UART framing/CRC/corrupt length/timeout/tick wrap/state/duplicate sessions; encoder forward/reverse/bounce/illegal transitions: PASS\n";
}
