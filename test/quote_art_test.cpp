#include "quote_scene.h"
#include "artwork_render.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
unsigned measure(const char*s){unsigned n=0;while(*s){vinyl::decodeUTF8(s);n+=8;}return n;}
int main(){
 vinyl::QuoteLayout q;q.wrap("Tuổi trẻ ơi",measure);assert(q.scale==512&&q.lines.count==1);
 q.wrap("Một thời thanh xuân",measure);assert(q.scale==384&&q.lines.count==1);
 std::string text="Một câu tiếng Việt dài phải chia thành nhiều dòng để người nghe đọc hết nội dung trên màn nhỏ";q.wrap(text.c_str(),measure);assert(q.scale>=256&&q.scale<=320&&q.lines.pages()>1);
 for(unsigned page=0;page<q.lines.pages();++page){for(unsigned i=0;i<q.rows(page);++i){char row[256];q.lines.copyRow(text.c_str(),page*2+i,row,sizeof row);assert(measure(row)*q.scale<=246*256);}}
 vinyl::QuoteMotion m;m.start(UINT32_MAX-99);assert(m.incoming(UINT32_MAX-99)==0&&m.outgoing(UINT32_MAX-99)==256);assert(m.incoming(0)>0&&m.incoming(0)<256);assert(m.incoming(0,1)<m.incoming(0,0));assert(m.incoming(400)==256&&m.outgoing(400)==0&&!m.moving(400));assert(!m.outgoing(1000000000));
 m.start(0,300);assert(m.duration==80&&m.incoming(100)==256&&m.outgoing(100)==0);
 for(unsigned x=0;x<256;++x)assert(vinyl::easeOutQ8(x,320)<=vinyl::easeOutQ8(x+1,320));
 assert(vinyl::mix565(0,0xffff,0)==0&&vinyl::mix565(0,0xffff,256)==0xffff);
 uint32_t pixels[2]={uint32_t(0x07e0)<<16|0xf800,uint32_t(0xffff)<<16|0x001f};
 assert(vinyl::sampleArtwork(pixels,2,0,0)==0xf800&&vinyl::sampleArtwork(pixels,2,256,0)==0x07e0&&vinyl::sampleArtwork(pixels,2,0,256)==0x001f);
 assert(vinyl::sampleArtwork(pixels,2,-100,-200)==0xf800&&vinyl::sampleArtwork(pixels,2,100000,200000)==0xffff);
 auto middle=vinyl::sampleArtwork(pixels,2,128,128);assert((middle>>11)==16&&((middle>>5)&63)==32&&(middle&31)==16);
 std::vector<uint32_t> image(96*96/2,0x5a695a69);for(int y=-18000;y<18000;y+=127)for(int x=-18000;x<18000;x+=113)assert(vinyl::sampleArtwork(image.data(),96,x,y)==0x5a69);
 std::cout<<"Quote typography: short/medium/long Vietnamese safe width; timed easing/row stagger/tick wrap; RGB565 bilinear exact pixels/blend/boundaries: PASS\n";
}
