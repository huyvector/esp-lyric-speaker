#include "lyric_layout.h"
#include "position_poll.h"
#include "avrc_sync_wire.h"
#include <cassert>
#include <string>
#include <iostream>
// Link-only callback doubles for the common wire header.
unsigned width(const char* s) { unsigned n=0; while(*s) {vinyl::decodeUTF8(s);n+=8;}return n; }
std::string row(const vinyl::LyricLayout& l,const char*text,unsigned i){char out[256];l.copyRow(text,i,out,sizeof out);return out;}
int main(){
 char comparison[]="Vài Lần Đón Đưa Á À Ạ Ắ Ễ Ỉ Ớ Ự Ỹ Đ";vinyl::vietnamese(comparison);vinyl::foldComparison(comparison);
 assert(!strcmp(comparison,"vai lan don dua a a a a e i o u y d"));
 vinyl::LyricLayout layout;
 const char* text="Tuổi trẻ này mình cùng nhau khoác vai đi từ sáng tới đêm";
 layout.wrap(text,128,width);assert(layout.count>2);
 std::string restored;
 for(unsigned i=0;i<layout.count;++i){auto s=row(layout,text,i);assert(width(s.c_str())<=128);if(i)restored+=' ';restored+=s;}
 assert(restored==text);
 const char* edge="hello you";layout.wrap(edge,64,width);assert(row(layout,edge,0)=="hello"&&row(layout,edge,1)=="you");
 const char* single="ếếếếếếếếếếếếếếếếế";layout.wrap(single,128,width);assert(layout.count==2&&width(row(layout,single,0).c_str())==128);
 assert(layout.pageAt(0,3000)==0);layout.wrap(text,128,width);
 assert(layout.pageAt(0,6000)==0&&layout.pageAt(6000,6000)==layout.pages()-1);
 layout.wrap("",128,width);assert(!layout.count);assert(row(layout,"",1).empty());
 vinyl::PositionPoll poll;assert(poll.request(0,1)==-1);poll.connect(true,0);
 assert(poll.request(1099,1)==-1);assert(poll.request(1100,1)==5);assert(poll.request(1300,1)==-1);
 assert(!poll.accept(4,1300,1,true));assert(poll.pending);
 assert(poll.accept(5,1400,1,true));assert(poll.request(3399,1)==-1&&poll.request(3400,1)==6);
 poll.invalidate(3500);assert(!poll.accept(6,3700,2,true));
 assert(poll.request(4600,2)==7);assert(!poll.accept(7,4700,1,true));assert(poll.accept(7,4700,2,true));
 assert(poll.request(6700,2)==8);assert(poll.request(9200,2)==-2);assert(!poll.accept(8,9201,2,true));
 assert(poll.request(12199,2)==-1);assert(poll.request(12200,2)==9);assert(poll.accept(9,12300,2,false));
 assert(poll.request(14300,2)==10);assert(poll.accept(10,14400,2,false));assert(poll.failures==3);
 assert(poll.request(29399,2)==-1&&poll.request(29400,2)==11);
 poll.connect(false,30000);assert(poll.request(60000,2)==-1);
 poll.connect(true,UINT32_MAX-100);assert(poll.request(998,3)==-1&&poll.request(999,3)==12);
 const uint8_t packet[]={0x30,0,0,9,0,3,0x81,0x32,0,1,0xd4,0xc0,1};
 vinyl_play_status_t sample{};assert(vinyl_parse_play_status(packet,sizeof packet,&sample));assert(sample.duration==229682&&sample.position==120000&&sample.state==1);
 for(size_t i=0;i<sizeof packet;++i)assert(!vinyl_parse_play_status(packet,i,&sample));
 uint8_t bad[sizeof packet];memcpy(bad,packet,sizeof bad);bad[3]=8;assert(!vinyl_parse_play_status(bad,sizeof bad,&sample));
 memcpy(bad,packet,sizeof bad);bad[1]=1;assert(!vinyl_parse_play_status(bad,sizeof bad,&sample));
 uint8_t event;uint32_t pos;uint8_t initial[]={0x31,0,0,5,5,0,1,0xd4,0xc0};
 assert(vinyl_parse_initial_notification(initial,sizeof initial,&event,&pos)&&event==5&&pos==120000);
 for(size_t i=0;i<sizeof initial;++i)assert(!vinyl_parse_initial_notification(initial,i,&event,&pos));
 uint8_t state[]={0x31,0,0,2,1,2};assert(vinyl_parse_initial_notification(state,sizeof state,&event,&pos)&&event==1&&pos==2);
 vinyl::Clock clock;clock.reset(0);clock.setPlaying(true,0);assert(clock.position(1000)==1000&&!clock.authoritative);
 clock.sync(120000,1000);assert(clock.position(2000)==121000&&clock.authoritative);
 clock.sync(40000,2000);assert(clock.position(2100)==40100);clock.setPlaying(false,2100);assert(clock.position(5000)==40100);
 std::cout<<"UTF-8 word wrap/pages; status wire bounds; poll labels, timeout, stale generation, backoff, rollover; seek/pause clock: PASS\n";
}
