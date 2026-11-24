#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include "quote_scene.h"
namespace vinyl {
// A single 28,800-byte workspace is shared by packed round scenes,
// RGB565 album sprites and indexed lyric frames. No frame-time allocation.
constexpr unsigned UI_WORK_BYTES=240*240/2;
constexpr uint16_t ROUND_PALETTE[16]={0,0x0841,0x1082,0x18c3,0x2104,0x2945,0x3186,0x4208,0x528a,0x630c,0x7bcf,0xa514,0xe58c,0xff59,0x8c91,0x67b5};
struct PackedCanvas {
 uint8_t* data;
 void pixel(int x,int y,unsigned c){if(x<0||x>=240||y<0||y>=240)return;unsigned at=y*240+x;uint8_t& b=data[at/2];b=at&1?(b&0xf0)|(c&15):(b&15)|((c&15)<<4);}
 unsigned pixel(int x,int y)const{unsigned at=y*240+x;return at&1?data[at/2]&15:data[at/2]>>4;}
 void box(int x,int y,int w,int h,unsigned c){for(int j=std::max(0,y);j<std::min(240,y+h);++j)for(int i=std::max(0,x);i<std::min(240,x+w);++i)pixel(i,j,c);}
 void dot(int x,int y,int radius,unsigned c){for(int j=-radius;j<=radius;++j)for(int i=-radius;i<=radius;++i)if(i*i+j*j<=radius*radius)pixel(x+i,y+j,c);}
 void line(int x,int y,int xx,int yy,unsigned c,int thick=1){int dx=std::abs(xx-x),dy=-std::abs(yy-y),sx=x<xx?1:-1,sy=y<yy?1:-1,err=dx+dy;for(;;){if(thick>1)dot(x,y,thick/2,c);else pixel(x,y,c);if(x==xx&&y==yy)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}}
 void circle(int x,int y,int r,unsigned c,int thick=1){int xx=r,yy=0,e=1-r;while(xx>=yy){for(int a:{-1,1})for(int b:{-1,1}){dot(x+a*xx,y+b*yy,thick/2,c);dot(x+a*yy,y+b*xx,thick/2,c);}++yy;if(e<0)e+=2*yy+1;else{--xx;e+=2*(yy-xx)+1;}}}
};
// Icons drawn from simple geometry; no copied NWatch artwork or bitmap fonts.
inline void menuIcon(PackedCanvas& c,unsigned id,int x,int y,int size,unsigned color){
 auto line=[&](int a,int b,int d,int e){c.line(x+a*size/32,y+b*size/32,x+d*size/32,y+e*size/32,color,std::max(1,size/13));};
 switch(id){
 case 0:case 1:{int dir=id==0?1:-1;line(-9*dir,-10,7*dir,0);line(7*dir,0,-9*dir,10);line(-9*dir,10,-9*dir,-10);line(11*dir,-10,11*dir,10);break;}
 case 2:case 4:line(-11,-10,11,-10);line(-11,-10,-11,7);line(-11,7,-5,7);line(-5,7,-5,12);line(-5,12,1,7);line(1,7,11,7);line(11,7,11,-10);line(-6,-4,6,-4);line(-6,1,id==2?2:6,1);if(id==2)c.dot(x+size/3,y+size/3,std::max(2,size/10),15);break;
 case 3:for(int i=-2;i<=2;++i){int height=i==0?12:std::abs(i)==1?7:4;line(i*5,-height,i*5,height);}break;
 case 5:line(-13,-5,-8,-10);line(-8,-10,8,-10);line(8,-10,13,-5);line(-8,1,-4,-3);line(-4,-3,4,-3);line(4,-3,8,1);c.dot(x,y+size/4,std::max(2,size/12),color);break;
 case 6:line(0,5,0,-12);line(0,-12,-7,-5);line(0,-12,7,-5);line(-12,3,-12,11);line(-12,11,12,11);line(12,11,12,3);break;
 case 7:line(10,-9,-2,-9);line(-2,-9,-11,0);line(-11,0,-2,9);line(-11,0,8,0);line(8,0,11,4);line(11,4,11,10);break;
 }
}
inline void visualIcon(PackedCanvas& c,unsigned id,int x,int y,int size,unsigned color){
 auto line=[&](int a,int b,int d,int e){c.line(x+a*size/32,y+b*size/32,x+d*size/32,y+e*size/32,color,std::max(1,size/18));};
 if(id==5){menuIcon(c,7,x,y,size,color);return;}
 if(id==0){c.circle(x,y,size/3,color,std::max(1,size/20));line(5,-12,12,-12);line(12,-12,12,-5);line(-5,12,-12,12);line(-12,12,-12,5);return;}
 if(id==1||id==4){constexpr int8_t wave[9]={0,-6,-10,-6,0,6,10,6,0};for(int row=-1;row<=1;row+=2)for(int n=0;n<8;++n){int a=wave[n],b=wave[n+1];if(id==4){a=(n%2?8:-8);b=-a;}line(-14+n*3,a/2+row*4,-11+n*3,b/2+row*4);}return;}
 if(id==2){constexpr int8_t points[8][2]={{-14,0},{-10,-5},{0,-7},{10,-5},{14,0},{10,5},{0,7},{-10,5}};for(int n=0;n<8;++n){auto& a=points[n];auto& b=points[(n+1)%8];line(a[0],a[1],b[0],b[1]);line(a[0]/2,a[1]*2,b[0]/2,b[1]*2);}return;}
 line(-12,0,12,0);line(0,-12,0,12);line(-7,-7,7,7);line(-7,7,7,-7);c.dot(x+size/3,y-size/3,std::max(1,size/20),color);
}
struct MenuMotion {
 int from=0,to=0;uint32_t began=0;bool initialized=false;
 int position(uint32_t now)const{unsigned a=easeOutQ8(now-began,220);return from+(to-from)*int(a)/256;}
 void select(unsigned item,uint32_t now,bool entering,unsigned count=8){int p=entering?int(item)*256:position(now);int target=int(item)*256;while(target-p>int(count)*128)target-=int(count)*256;while(target-p<-int(count)*128)target+=int(count)*256;from=p;to=target;began=now;initialized=true;}
 bool moving(uint32_t now)const{return initialized&&uint32_t(now-began)<220;}
};
struct QuoteMap {int sx=0,step=256,top=0,height=18,sy=0,dx=0;unsigned alpha=256,reveal=284;};
inline QuoteMap quoteMap(unsigned scale,unsigned rows,unsigned row,int dy,int dx,unsigned alpha,unsigned effect){
 QuoteMap m;m.step=65536/int(scale);m.height=(18*scale+255)/256;m.top=42-int(rows*m.height+(rows>1?3:0))/2+int(row*(m.height+3))+dy;m.sx=142*256-(142+dx)*m.step;m.sy=(20+int(row)*20)*256;m.alpha=alpha;m.reveal=effect==1?alpha*304/256:304;return m;
}
}
