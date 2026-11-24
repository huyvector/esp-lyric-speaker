#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
namespace vinyl {
inline uint16_t artworkPixel(const uint32_t*words,int size,int x,int y){size_t at=size_t(y)*size+x;return uint16_t(words[at/2]>>((at%2)*16));}
inline uint16_t sampleArtwork(const uint32_t*words,int size,int xQ8,int yQ8){
 xQ8=std::max(0,std::min((size-1)*256,xQ8));yQ8=std::max(0,std::min((size-1)*256,yQ8));
 int x=xQ8>>8,y=yQ8>>8,x1=std::min(size-1,x+1),y1=std::min(size-1,y+1);
 unsigned fx=xQ8&255,fy=yQ8&255;
 uint16_t a=artworkPixel(words,size,x,y),b=artworkPixel(words,size,x1,y),c=artworkPixel(words,size,x,y1),d=artworkPixel(words,size,x1,y1);
 unsigned wa=(256-fx)*(256-fy),wb=fx*(256-fy),wc=(256-fx)*fy,wd=fx*fy;
 unsigned r=((a>>11)*wa+(b>>11)*wb+(c>>11)*wc+(d>>11)*wd+32768)>>16;
 unsigned g=(((a>>5)&63)*wa+((b>>5)&63)*wb+((c>>5)&63)*wc+((d>>5)&63)*wd+32768)>>16;
 unsigned blue=((a&31)*wa+(b&31)*wb+(c&31)*wc+(d&31)*wd+32768)>>16;
 return uint16_t(r<<11|g<<5|blue);
}
inline uint16_t coverLight(uint16_t color,int x,int y){unsigned shine=unsigned(std::max(0,28-std::abs(x-y-8)))/10;
 unsigned r=std::min(31u,unsigned(color>>11)+shine/2),g=std::min(63u,unsigned((color>>5)&63)+shine),b=std::min(31u,unsigned(color&31)+shine/2);
 return uint16_t(r<<11|g<<5|b);
}
}
