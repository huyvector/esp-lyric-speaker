#include "render_ui.h"
#include "setup_display.h"
#include "lyric_layout.h"
#include "quote_scene.h"
#include "artwork_render.h"
#include "record_animation.h"
#include "vinyl_skin.h"
#include "pins.h"
#include "ui_canvas.h"
#include "ui_assets.h"
#include "visualizer.h"
#include "lcd_options.h"
#include "connection_icons.h"
#include "device_identity.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <U8g2lib.h>
#include <cmath>
#include <initializer_list>
#ifdef TFT_MOSI
// The two panels share SPI; TFT_eSPI build flags must match the strip driver.
static_assert(TFT_MOSI==DISPLAY_MOSI && TFT_SCLK==DISPLAY_SCLK &&
              TFT_CS==ROUND_CS && TFT_DC==DISPLAY_DC && TFT_RST==DISPLAY_RST,
              "TFT_eSPI pin flags differ from display/include/pins.h");
#endif
TFT_eSPI tft;
namespace {
uint8_t lcdFlags=vinyl::DEFAULT_LCD_FLAGS;uint32_t infoUntil=0;
constexpr uint16_t BG=0x0821,CREAM=0xff59,GOLD=0xe58c,MUTED=0x8c91;
class Strip {
 void select(){SPI.beginTransaction(SPISettings(40000000,MSBFIRST,SPI_MODE0));digitalWrite(ROUND_CS,HIGH);digitalWrite(LYRIC_CS,LOW);}
 void deselect(){digitalWrite(LYRIC_CS,HIGH);SPI.endTransaction();}
 void cmd(uint8_t v,const uint8_t*p=nullptr,size_t n=0){digitalWrite(DISPLAY_DC,LOW);SPI.write(v);digitalWrite(DISPLAY_DC,HIGH);if(n)SPI.writeBytes(p,n);}
 void setting(uint8_t op,std::initializer_list<uint8_t> data){select();cmd(op,data.begin(),data.size());deselect();}
public:
 // Portrait 76x284 has origin 82/18; landscape swaps origin to 18/82.
 void begin(){setting(0x01,{});delay(150);setting(0x11,{});delay(120);setting(0x3a,{0x55});setting(0x36,{0x60});setting(0xb2,{0x0c,0x0c,0,0x33,0x33});setting(0xb7,{0x35});setting(0xbb,{0x28});setting(0xc0,{0x2c});setting(0xc2,{1});setting(0xc3,{0x10});setting(0xc4,{0x20});setting(0xc6,{0x0f});setting(0xd0,{0xa4,0xa1});setting(0xe0,{0xf0,0x05,0x0a,0x06,0x06,0x03,0x2b,0x32,0x43,0x36,0x11,0x10,0x2b,0x32});setting(0xe1,{0xf0,0x08,0x0c,0x0b,0x09,0x24,0x2b,0x22,0x43,0x38,0x15,0x16,0x2f,0x37});setting(0x20,{});setting(0x13,{});setting(0x29,{});delay(20);}
 void options(uint8_t flags){setting(0x36,{uint8_t((flags&2?0xa0:0x60)|(flags&1?8:0))});setting(flags&4?0x21:0x20,{});}
 void row(unsigned x,unsigned y,unsigned n,const uint16_t*p){if(x>=284||y>=76||!n||n>284-x)return;uint8_t win[4],bytes[568];for(unsigned i=0;i<n;++i){bytes[i*2]=p[i]>>8;bytes[i*2+1]=p[i]&255;}select();// Window coordinates are big-endian on the wire.
 unsigned a=x+18,b=x+n-1+18;win[0]=a>>8;win[1]=a;win[2]=b>>8;win[3]=b;cmd(0x2a,win,4);a=y+82;win[0]=a>>8;win[1]=a;win[2]=a>>8;win[3]=a;cmd(0x2b,win,4);cmd(0x2c,bytes,n*2);deselect();}
 void frame(unsigned first,unsigned rows,const uint8_t*indices){if(first>=76||!rows||first+rows>76)return;uint8_t win[4],bytes[568];select();unsigned a=18,b=301;win[0]=a>>8;win[1]=a;win[2]=b>>8;win[3]=b;cmd(0x2a,win,4);a=first+82;b=a+rows-1;win[0]=a>>8;win[1]=a;win[2]=b>>8;win[3]=b;cmd(0x2b,win,4);cmd(0x2c);
  for(unsigned y=first;y<first+rows;++y){for(unsigned x=0;x<284;++x){uint16_t c=vinyl::LYRIC_PALETTE[indices[y*284+x]];bytes[x*2]=c>>8;bytes[x*2+1]=c;}SPI.writeBytes(bytes,sizeof bytes);}deselect();}

}strip;
uint8_t noop(u8x8_t*,uint8_t,uint8_t,void*){return 1;}
struct Bitmap {u8g2_t u{};u8x8_display_info_t info{};uint8_t bits[2880]{};
 void begin(){u8g2_Setup_ssd1306_i2c_128x32_univision_f(&u,U8G2_R0,noop,noop);info=*u.u8x8.display_info;info.tile_width=36;info.tile_height=10;info.pixel_width=284;info.pixel_height=76;u.u8x8.display_info=&info;u8g2_SetupBuffer(&u,bits,10,u8g2_ll_hvline_vertical_top_lsb,&u8g2_cb_r0);font();u8g2_SetDrawColor(&u,1);}
 void font(){u8g2_SetFont(&u,u8g2_font_unifont_t_vietnamese1);}void clear(){memset(bits,0,sizeof bits);}unsigned width(const char*s){return u8g2_GetUTF8Width(&u,s);}void text(int x,int y,const char*s){u8g2_DrawUTF8(&u,x,y,s);}bool pixel(int x,int y)const{return x>=0&&x<284&&y>=0&&y<76&&(bits[(y/8)*288+x]&(1<<(y%8)));}
 void centered(const char*s,int y,unsigned widthLimit=270){char tmp[256];strlcpy(tmp,s,sizeof tmp);while(width(tmp)>widthLimit&&tmp[0])tmp[vinyl::utf8Prefix(tmp,strlen(tmp)-1)]=0;text((284-int(width(tmp)))/2,y,tmp);}
}bitmap;
bool menu=false,force=true,labelDirty=true,baseDirty=true;int selection=0,stripY=76,labelY=56;uint32_t lastRound=0,lastStrip=0,uiNow=0;
vinyl::RecordMotion recordMotion;vinyl::RecordTransform recordTransform;
union Workspace {uint8_t bytes[vinyl::UI_WORK_BYTES];uint16_t rgb[vinyl::UI_WORK_BYTES/2];}work{};
vinyl::PackedCanvas canvas{work.bytes};
enum class Job {Idle,Album,Lyric,Base};Job job=Job::Idle;unsigned composeY=0;uint32_t jobGeneration=0;
uint32_t recordFrames=0,lyricFrames=0,composeMaxUs=0,roundPresentMaxUs=0,stripPresentMaxUs=0,skippedRows=0;
uint32_t roundHashes[240]{},stripHashes[76]{};bool roundValid=false,stripValid=false;uint8_t roundBrightness=255;
vinyl::VolumePopup popup;bool ringDirty=false,ringWasVisible=false;uint32_t ringAt=0,progressAt=0;bool progressDirty=true,iconDirty=true;bool iconPlaying=false;uint32_t progressPosition=~0u,progressDuration=~0u;
vinyl::MenuMotion menuMotion;bool menuDirty=false,menuSettling=false,menuEffects=false;uint32_t menuAt=0,menuOpened=0;
bool toneEditor=false,toneDirty=false,toneAvailable=false;int toneValue=2;
const char* menuLabels[]={"NEXT TRACK","PREVIOUS TRACK","AUTO LYRICS","VISUALIZER","LYRICS ONLY","WI-FI SETUP","UPDATE / IP","BASS BOOST","BACK"};
const char* effectLabels[]={"AUTO ROTATE","RIBBONS","ORBIT","STARFIELD","WAVES","BACK"};
vinyl::VisualScene visualScene;
bool transportVisible=false;int transportDirection=1;uint32_t transportAt=0,jobNow=0,jobTransportAt=0;bool jobTransport=false;int jobDirection=1;
struct QuoteView {int kind=-1,index=-2;unsigned page=0,pages=1,scale=256,rows=1,meter=0,peak=0,progress=0;uint32_t generation=0,trackProgress=0;};
Bitmap quoteBitmap;
QuoteView quoteView;
vinyl::QuoteLayout typography;
vinyl::LyricLayout& layout=typography.lines;
vinyl::QuoteMotion quoteMotion;
uint32_t quoteFrameAt=0;unsigned stripFirst=0,stripLast=75;
unsigned frameIn[2]={256,256},frameOut=0;
int frameInY[2]{},frameOutY=0;
vinyl::QuoteMap inMap[2];unsigned quoteEffect=0;
char currentLine[256]{};
unsigned coverage(const Bitmap&canvas,int xQ8,int yQ8){
 int x=xQ8>>8,y=yQ8>>8;unsigned fx=xQ8&255,fy=yQ8&255;
 return ((canvas.pixel(x,y)?(256-fx)*(256-fy):0)+(canvas.pixel(x+1,y)?fx*(256-fy):0)+(canvas.pixel(x,y+1)?(256-fx)*fy:0)+(canvas.pixel(x+1,y+1)?fx*fy:0)+128)>>8;
}
void prepareStrip(uint32_t now){
 char candidate[256]{};int64_t position=int64_t(playClock.position(now))+offsetMs;
 int index=loadingLyrics?-1:lyrics.at(position);if(index>=0)lyrics.copyLine(index,candidate,sizeof candidate);
 bool vu=oledMode==vinyl::OLEDMode::VU||(oledMode==vinyl::OLEDMode::Auto&&(!candidate[0]||loadingLyrics));
 uint32_t remaining=2000;QuoteView next;next.generation=track.generation;next.kind=vu?0:candidate[0]?1:2;next.index=index;next.trackProgress=playClock.duration?unsigned(uint64_t(std::min(playClock.position(now),playClock.duration))*268/playClock.duration):0;
 if(next.kind==1){
  if(strcmp(candidate,currentLine)||quoteView.generation!=track.generation||quoteView.index!=index){strlcpy(currentLine,candidate,sizeof currentLine);typography.wrap(currentLine,[](const char*s){return quoteBitmap.width(s);});}
  int64_t start=lyrics.cues[index].ms,end=index+1<lyrics.count?lyrics.cues[index+1].ms:(playClock.duration>start?playClock.duration:start+6000);
  uint32_t span=uint32_t(std::max(int64_t(1),end-start)),elapsed=uint32_t(std::max(int64_t(0),position-start));
  next.page=layout.pageAt(elapsed,span);next.pages=layout.pages();next.scale=typography.scale;next.rows=typography.rows(next.page);if(next.rows>1)next.scale=std::min(next.scale,320u);
  remaining=uint32_t(std::max(int64_t(0),int64_t(uint64_t(span)*(next.page+1)/std::max(1u,next.pages))-elapsed));
  next.progress=unsigned(std::min(uint64_t(256),uint64_t(elapsed)*256/span));
 }
 bool changed=next.generation!=quoteView.generation||next.kind!=quoteView.kind||next.index!=quoteView.index||next.page!=quoteView.page||next.scale!=quoteView.scale;
 if(changed){quoteMotion.start(now,remaining);quoteMotion.exit=0;quoteEffect=unsigned(std::max(0,index)+next.page)%4;}
 if(vu)visualScene.prepare(visualMode,now,audioState.peak,playClock.playing&&linkOnline);
 bool progressChanged=next.trackProgress!=quoteView.trackProgress;
 // Keep only the title in the header; the bottom two rows are a track bar.
 bitmap.clear();bitmap.font();bitmap.centered(track.title[0]?track.title:"ESP LYRIC SPEAKER",14);
 bool redraw=changed||force;
 quoteView=next;if(redraw){quoteBitmap.clear();quoteBitmap.font();

 if(next.kind==1){char row[256];for(unsigned i=0;i<next.rows;++i){layout.copyRow(currentLine,next.page*2+i,row,sizeof row);quoteBitmap.centered(row,34+i*20);}}
 else if(next.kind==2){quoteBitmap.centered(index>=0?"INSTRUMENTAL":lyricStatus,34);}

 }
 stripFirst=0;stripLast=75;
 if(!redraw){
  if(quoteMotion.active||vu){stripFirst=18;stripLast=progressChanged?75:71;}
  else if(progressChanged){stripFirst=74;stripLast=75;}
  else {stripFirst=76;stripLast=75;}
 }
 // Current text appears immediately at >=87.5% opacity. No old/new overlap,
 // zoom or moving scale; Q8 offsets keep the short motion continuous.
 frameOut=0;
 for(unsigned i=0;i<2;++i){
  frameIn[i]=quoteMotion.incoming(now,i);unsigned alpha=224+frameIn[i]/8;
  int displacement=256-int(frameIn[i]),dxQ8=0,dyQ8=0;
  if(quoteEffect==0)dyQ8=displacement*3;
  if(quoteEffect==2)dyQ8=-displacement*2;
  if(quoteEffect==3)dxQ8=displacement*3;
  inMap[i]=vinyl::quoteMap(quoteView.scale,quoteView.rows,i,0,0,alpha,0);
  inMap[i].top-=1;inMap[i].height+=8;inMap[i].sy-=4*inMap[i].step;
  inMap[i].sx-=dxQ8*inMap[i].step/256;inMap[i].sy-=dyQ8*inMap[i].step/256;
 }
 if(!quoteMotion.moving(now))quoteMotion.active=false;
 stripY=0;force=false;
}
unsigned mappedCoverage(const Bitmap& image,const vinyl::QuoteMap& m,int x,int y){
 if(!m.alpha||y<m.top||y>=m.top+m.height||unsigned(x)>m.reveal)return 0;
 unsigned a=coverage(image,m.sx+x*m.step,m.sy+(y-m.top)*m.step)*m.alpha/256;
 if(unsigned(x)+12>m.reveal)a=a*std::min(12u,m.reveal-unsigned(x))/12;
 return a;
}
uint16_t stripPixel(int x,int y){
 uint16_t color=vinyl::rgb(7+y/28,10+y/24,16+y/20);
 if(y<18)return bitmap.pixel(x,y)?CREAM:color;
 if(y>=72){if(y>=74&&x>=8&&x<276)return unsigned(x-8)<quoteView.trackProgress?GOLD:uint16_t(0x1924);return color;}
 if(quoteView.kind>0){
  for(unsigned row=0;row<quoteView.rows;++row){unsigned a=mappedCoverage(quoteBitmap,inMap[row],x,y);color=vinyl::mix565(color,quoteView.kind==1?CREAM:MUTED,a);}
  if(quoteView.kind==1){
   int qx=x<142?x-5:278-x,qy=x<142?y-21:68-y;
   if(qx>=0&&qx<8&&qy>=0&&qy<8&&((qx%5<3&&qy<4)||(qx%5<2&&qy>=4)))color=vinyl::mix565(color,GOLD,frameIn[0]*3/4);
  }
 }
 return color;
}

uint32_t hashRow(const uint8_t* p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
void presentRound(){
 unsigned first=240,last=0;for(unsigned y=0;y<240;++y){uint32_t h=hashRow(work.bytes+y*120,120);if(!roundValid||h!=roundHashes[y]){first=std::min(first,y);last=y;}roundHashes[y]=h;}roundValid=true;
 if(first==240){skippedRows+=240;return;}skippedRows+=240-(last-first+1);
 uint32_t began=micros();uint16_t row[240];tft.startWrite();tft.setAddrWindow(0,first,240,last-first+1);
 for(unsigned y=first;y<=last;++y){for(int x=0;x<240;++x)row[x]=vinyl::ROUND_PALETTE[canvas.pixel(x,y)];tft.pushPixels(row,240);}tft.endWrite();roundPresentMaxUs=std::max(roundPresentMaxUs,uint32_t(micros()-began));ringDirty=popup.visible;
}
void presentStrip(){
 unsigned first=76,last=0;for(unsigned y=stripFirst;y<=stripLast;++y){uint32_t h=hashRow(work.bytes+y*284,284);if(!stripValid||h!=stripHashes[y]){first=std::min(first,y);last=y;}stripHashes[y]=h;}stripValid=true;
 if(first<76){uint32_t began=micros();strip.frame(first,last-first+1,work.bytes);stripPresentMaxUs=std::max(stripPresentMaxUs,uint32_t(micros()-began));skippedRows+=76-(last-first+1);++lyricFrames;}else skippedRows+=76;
}
void maskRound(const char* text,int y,unsigned color,unsigned scale=256){
 bitmap.clear();bitmap.font();bitmap.centered(text,18,270);
 int width=284*int(scale)/256,height=22*int(scale)/256,left=120-width/2;
 for(int yy=0;yy<height;++yy)for(int x=0;x<width;++x)if(bitmap.pixel(x*256/int(scale),yy*256/int(scale)))canvas.pixel(left+x,y+yy,color);
}
void progressRing(uint32_t now){
 uint32_t duration=playClock.duration,position=duration?std::min(playClock.position(now),duration):0;
 if(!progressDirty&&duration==progressDuration&&position/250==progressPosition/250){progressAt=now;return;}
 unsigned threshold=duration?uint64_t(position)*65536/duration:0;
 for(unsigned i=0;i<sizeof vinyl::PLAY_RING_ANGLE/sizeof *vinyl::PLAY_RING_ANGLE;++i){
  uint16_t c=vinyl::PLAY_RING_ANGLE[i]<threshold?0x67b5:0x18c3;
  if(threshold&&threshold<65536&&vinyl::PLAY_RING_ANGLE[i]<threshold&&threshold-vinyl::PLAY_RING_ANGLE[i]<700)c=CREAM;
  work.rgb[i]=vinyl::mix565(0,c,vinyl::PLAY_RING_COVER[i]*256/255);
 }
 tft.startWrite();for(const auto& span:vinyl::PLAY_RING_SPANS){tft.setAddrWindow(span.x,span.y,span.count,1);tft.pushPixels(work.rgb+span.offset,span.count);}tft.endWrite();
 progressAt=now;progressPosition=position;progressDuration=duration;progressDirty=false;
}
void playbackIcon(){
 bool playing=playClock.playing;if(!iconDirty&&playing==iconPlaying)return;
 for(int y=0;y<32;++y)for(int x=0;x<32;++x){int dx=x-16,dy=y-16;uint16_t c=vinyl::roundLift(vinyl::recordPixel(x+104-120,y+185-120));
  if(dx*dx+dy*dy<14*14)c=BG;
  bool mark=playing?((dx>=-5&&dx<=-2||dx>=3&&dx<=6)&&std::abs(dy)<=6):(dx>=-4&&dx<=7&&std::abs(dy)*11<=(7-dx)*7);
  work.rgb[y*32+x]=mark?CREAM:c;
 }
 tft.startWrite();tft.pushImage(104,185,32,32,work.rgb);tft.endWrite();iconDirty=false;iconPlaying=playing;
}
uint16_t transportPixel(uint16_t color,int x,int y,uint32_t now){
 if(!jobTransport)return color;unsigned age=now-jobTransportAt;if(age>=650)return color;
 unsigned alpha=age<80?vinyl::easeOutQ8(age,80):age>480?(650-age)*256/170:256;
 if(x*x+y*y<25*25)color=vinyl::mix565(color,BG,alpha*230/256);
 int xx=x*jobDirection;bool mark=(xx>=-11&&xx<=7&&std::abs(y)*18<=(7-xx)*10)||(xx>=10&&xx<=13&&std::abs(y)<=10);
 return mark?vinyl::mix565(color,GOLD,alpha):color;
}
void volumeRing(uint32_t now){
 // Geometry/angles are in flash. Render all rim pixels before touching SPI.
 unsigned fade=popup.visible?256-popup.slide(now)*8:0;unsigned value=popup.visible?std::max(0,popup.valueQ8(now)):0;
 unsigned threshold=uint64_t(value)*65536/(127*256);uint16_t lit=popup.limit&&((now-popup.touched)/100)%2?CREAM:GOLD;
 for(unsigned i=0;i<sizeof vinyl::RIM_ANGLE/sizeof *vinyl::RIM_ANGLE;++i){uint16_t c=vinyl::RIM_ANGLE[i]<threshold?lit:0x2945;unsigned alpha=fade*vinyl::RIM_COVER[i]/255;work.rgb[i]=vinyl::mix565(0,c,alpha);}
 tft.startWrite();for(const auto& span:vinyl::RIM_SPANS){tft.setAddrWindow(span.x,span.y,span.count,1);tft.pushPixels(work.rgb+span.offset,span.count);}tft.endWrite();ringAt=now;ringDirty=false;ringWasVisible=popup.visible;
}
void menuFrame(uint32_t now){
 memset(work.bytes,0,sizeof work.bytes);unsigned intro=vinyl::easeOutQ8(now-menuOpened,280);
 maskRound(menuEffects?"VISUAL / MENU":"SPEAKER / MENU",24,14);int count=menuEffects?vinyl::VISUAL_MENU_COUNT:vinyl::MAIN_MENU_COUNT;int pos=menuMotion.position(now),nearest=(pos+128)/256;
 for(int delta=-3;delta<=3;++delta){int item=nearest+delta,d=item*256-pos;if(std::abs(d)>780)continue;int x=120+d*62/256,y=106+(d/16)*(d/16)/100+int((256-intro)*22/256);unsigned focus=256-std::min(256,std::abs(d));int size=26+focus*22/256;
  if(std::abs(d)<128){canvas.circle(x,y,34+int(focus)*5/256,3);canvas.circle(x,y,35+int(focus)*5/256,2);}
  unsigned id=unsigned((item%count+count)%count),color=std::abs(d)<128?12:14;
  if(menuEffects)vinyl::visualIcon(canvas,id,x,y,size,color);else if(id==3)vinyl::visualIcon(canvas,2,x,y,size,color);else if(id==7){for(int k=-1;k<=1;++k){int xx=x+k*size/4;canvas.line(xx,y-size/3,xx,y+size/3,color,2);canvas.dot(xx,y+(k==0?-1:1)*size/6,std::max(2,size/12),color);}}else vinyl::menuIcon(canvas,id==8?7:id,x,y,size,color);
 }
 canvas.line(108,61,132,61,12,2);canvas.line(108,154,132,154,12,2);
 maskRound(menuEffects?effectLabels[selection]:menuLabels[selection],169,13);char number[12];snprintf(number,sizeof number,"%d / %d",selection+1,count);maskRound(number,190,12);maskRound("TURN / CLICK",210,14);
 presentRound();menuDirty=false;menuSettling=menuMotion.moving(now)||now-menuOpened<300;menuAt=now;
}
void toneFrame(){
 memset(work.bytes,0,sizeof work.bytes);maskRound("SOUND",25,14);maskRound("BASS BOOST",57,12);
 if(toneAvailable){char value[12];snprintf(value,sizeof value,"%d / 3",toneValue);maskRound(value,91,13,512);
  canvas.line(42,157,198,157,3,2);for(int n=0;n<4;++n){int x=42+n*52;canvas.line(x,152,x,162,n==toneValue?12:4,2);}canvas.dot(42+toneValue*52,157,5,12);
  const char* names[]={"OFF","LOW","MEDIUM","HIGH"};maskRound(names[toneValue],176,14);
 }else{maskRound("AUDIO OFFLINE",97,13);maskRound("CHECK SPEAKER LINK",130,14);}
 if(toneAvailable)maskRound("TURN TO ADJUST",193,14);maskRound("CLICK: DONE",211,12);presentRound();toneDirty=false;
}
enum class ConnectionScene { None, Boot, Joining, Portal, Scanning, Failed, Online, Audio, Pair, Ready };
ConnectionScene connectionScene=ConnectionScene::None;uint32_t connectionAt=0;bool wifiReady=false,wifiIconDirty=true;
struct MaskCanvas {Bitmap& b;
 void line(int x,int y,int xx,int yy,unsigned,int thick=1){for(int n=-(thick/2);n<=thick/2;++n)u8g2_DrawLine(&b.u,x,y+n,xx,yy+n);}
 void dot(int x,int y,int r,unsigned){u8g2_DrawDisc(&b.u,x,y,r,U8G2_DRAW_ALL);}
 void circle(int x,int y,int r,unsigned,int thick=1){for(int n=0;n<thick;++n)u8g2_DrawCircle(&b.u,x,y,r+n,U8G2_DRAW_ALL);}
};
void stripText(const char* text,int y,unsigned width=208){char line[256];strlcpy(line,text,sizeof line);while(bitmap.width(line)>width&&line[0])line[vinyl::utf8Prefix(line,strlen(line)-1)]=0;bitmap.text(68,y,line);}
void connectionFrame(ConnectionScene scene,const char* detail,uint32_t now,bool roundPanel=true){
 job=Job::Idle;stripY=76;labelY=56;connectionScene=scene;connectionAt=now;bool setup=scene==ConnectionScene::Portal||scene==ConnectionScene::Scanning;
 const char* title="STARTING";const char* subtitle="ESP LYRIC SPEAKER";const char* heading="SPEAKER STATUS";const char* main="Starting up...";const char* foot="Please wait";
 auto icon=vinyl::ConnectionIcon::Music,stripIcon=vinyl::ConnectionIcon::Music;
 switch(scene){
 case ConnectionScene::Joining:icon=vinyl::ConnectionIcon::WiFi;stripIcon=vinyl::ConnectionIcon::Link;title="CONNECTING";subtitle="YOUR WI-FI NETWORK";heading="NETWORK";main=detail;foot="Waiting for connection";break;
 case ConnectionScene::Portal:case ConnectionScene::Scanning:icon=vinyl::ConnectionIcon::WiFi;stripIcon=vinyl::ConnectionIcon::Browser;title=scene==ConnectionScene::Scanning?"SCANNING WI-FI":"WI-FI SETUP";subtitle=vinyl::SETUP_SSID;heading="OPEN IN BROWSER";main="192.168.4.1";foot="Choose your Wi-Fi network";break;
 case ConnectionScene::Failed:icon=vinyl::ConnectionIcon::Warning;stripIcon=vinyl::ConnectionIcon::Browser;title="TRY AGAIN";subtitle="WI-FI NOT CONNECTED";heading="CONNECTION FAILED";main=detail;foot="Open 192.168.4.1 to retry";break;
 case ConnectionScene::Online:icon=vinyl::ConnectionIcon::Check;stripIcon=vinyl::ConnectionIcon::Link;title="WI-FI READY";subtitle="YOU ARE ONLINE";heading="LOCAL IP ADDRESS";main=detail;foot="Lyrics and artwork enabled";break;
 case ConnectionScene::Audio:icon=vinyl::ConnectionIcon::Link;stripIcon=vinyl::ConnectionIcon::Music;title="CONNECT AUDIO";subtitle="WAITING FOR SPEAKER";heading="AUDIO BOARD OFFLINE";main="Check power and UART";foot="Bluetooth on audio board";break;
 case ConnectionScene::Pair:icon=vinyl::ConnectionIcon::Bluetooth;stripIcon=vinyl::ConnectionIcon::Phone;title="READY TO PAIR";subtitle="BLUETOOTH";heading="ON YOUR PHONE";main=vinyl::BLUETOOTH_NAME;foot="Select it in Bluetooth";break;
 case ConnectionScene::Ready:icon=vinyl::ConnectionIcon::Check;stripIcon=vinyl::ConnectionIcon::Music;title="CONNECTED";subtitle="READY FOR YOUR MUSIC";heading="PLAY FROM YOUR PHONE";main="Open Spotify and play";foot=wifiReady?"Lyrics appear here":"Wi-Fi off. Audio ready";break;
 default:break;
 }
 if(roundPanel){memset(work.bytes,0,sizeof work.bytes);maskRound("ESP LYRIC",26,14);
 unsigned pulse=(now/160)%12;int radius=45+int(pulse<6?pulse:11-pulse);canvas.circle(120,100,radius,2);canvas.circle(120,100,55,1);
 vinyl::connectionIcon(canvas,icon,120,100,62,scene==ConnectionScene::Failed?12:15);maskRound(title,163,13);maskRound(subtitle,185,14);
 if(setup){char key[40];snprintf(key,sizeof key,"KEY: %s",vinyl::SETUP_PASSWORD);maskRound(key,203,12);}
 else if(scene==ConnectionScene::Boot||scene==ConnectionScene::Joining){for(int n=0;n<3;++n)canvas.dot(108+n*12,218,2,n==int((now/320)%3)?12:3);}
 else {vinyl::connectionIcon(canvas,vinyl::ConnectionIcon::WiFi,106,218,14,(wifiReady||scene==ConnectionScene::Online)?15:3);vinyl::connectionIcon(canvas,vinyl::ConnectionIcon::Bluetooth,134,218,14,scene==ConnectionScene::Ready?15:3);}
 presentRound();}
 bitmap.clear();bitmap.font();MaskCanvas mask{bitmap};vinyl::connectionIcon(mask,stripIcon,30,38,37,1);stripText(heading,16);if(setup||scene==ConnectionScene::Online)bitmap.text(170-int(bitmap.width(main))/2,43,main);else stripText(main,43);stripText(foot,68);
 for(int y=0;y<76;++y)for(int x=0;x<284;++x){uint16_t c=vinyl::rgb(7+y/28,10+y/24,16+y/20);bool ink=bitmap.pixel(x,y);unsigned alpha=ink?256:0;
  if((setup||scene==ConnectionScene::Online)&&x>=64&&y>=24&&y<56)alpha=coverage(bitmap,(x-170)*256*256/384+170*256,(y-24)*256*256/384+27*256);
  c=vinyl::mix565(c,x<58?GOLD:y<20||y>57?MUTED:CREAM,alpha);work.bytes[y*284+x]=vinyl::LYRIC_INDEX[c];
 }
 strip.frame(0,76,work.bytes);stripValid=false;
}
void wifiBadge(){
 if(!wifiIconDirty)return;memset(work.bytes,0,sizeof work.bytes);vinyl::connectionIcon(canvas,vinyl::ConnectionIcon::WiFi,120,42,19,1);if(!wifiReady)canvas.line(112,32,129,50,1,1);
 for(int y=0;y<32;++y)for(int x=0;x<32;++x){int dx=x-16,dy=y-16;bool ink=canvas.pixel(104+x,26+y);uint16_t c=dx*dx+dy*dy<14*14?BG:vinyl::roundLift(vinyl::recordPixel(x-16,y-94));work.rgb[y*32+x]=ink?(wifiReady?0x67b5:MUTED):c;}
 tft.startWrite();tft.pushImage(104,26,32,32,work.rgb);tft.endWrite();wifiIconDirty=false;
}
void compose(uint32_t now){
 uint32_t began=micros();unsigned rows=0;
 while(rows<16){
  if(job==Job::Album){int y=int(composeY)-56,sx,sy;recordTransform.rowStart(-56,y,sx,sy);unsigned chord=vinyl::recordHalfWidth(y);
   for(int x=-56;x<56;++x){uint16_t c;if(x<-int(chord)||x>=int(chord))c=vinyl::recordPixel(x,y);else{c=haveArtwork?vinyl::sampleArtwork(artworkWords,vinyl::ART_SIZE,sx/256,sy/256):uint16_t(0xc514);c=vinyl::coverLight(c,x,y);int r=x*x+y*y;if(r<9)c=0x1082;if(r<2)c=0xdefb;}work.rgb[composeY*112+x+56]=transportPixel(vinyl::roundLift(c),x,y,jobNow);sx+=recordTransform.xx;sy+=recordTransform.yx;}
  }else if(job==Job::Lyric){if(quoteView.kind==0&&composeY>=18&&composeY<=vinyl::VISUAL_BOTTOM)visualScene.row(composeY,work.bytes+composeY*284);else for(int x=0;x<284;++x)work.bytes[composeY*284+x]=vinyl::LYRIC_INDEX[stripPixel(x,composeY)];}
  else if(job==Job::Base){for(int x=0;x<240;++x){uint16_t c=vinyl::roundLift(vinyl::recordPixel(x-120,int(composeY)-120));unsigned best=0,distance=65536;for(unsigned i=0;i<12;++i){unsigned d=std::abs(int(c>>11)-int(vinyl::ROUND_PALETTE[i]>>11));if(d<distance){distance=d;best=i;}}canvas.pixel(x,composeY,best);}}

  else return;
  ++composeY;++rows;if(job==Job::Lyric)stripY=composeY;
  unsigned end=job==Job::Album?112:job==Job::Lyric?stripLast+1:240;
  if(composeY==end){Job completed=job;job=Job::Idle;
   if(completed==Job::Album){uint32_t start=micros();tft.startWrite();tft.pushImage(64,64,112,112,work.rgb);tft.endWrite();roundPresentMaxUs=std::max(roundPresentMaxUs,uint32_t(micros()-start));++recordFrames;labelY=56;}
   else if(completed==Job::Lyric){presentStrip();stripY=76;}
   else{roundValid=false;presentRound();baseDirty=false;progressDirty=iconDirty=wifiIconDirty=true;}
   break;
  }
  if((rows&1)==0&&uint32_t(micros()-began)>=2000)break;
 }
 composeMaxUs=std::max(composeMaxUs,uint32_t(micros()-began));
}

}
void uiDirty(){force=true;labelDirty=true;wifiIconDirty=true;progressDirty=iconDirty=true;if(job!=Job::Idle){job=Job::Idle;stripY=76;labelY=56;}}
void uiRestore(){infoUntil=0;connectionScene=ConnectionScene::None;baseDirty=true;roundValid=false;uiDirty();}
void uiLcdOptions(uint8_t flags){lcdFlags=flags&7;strip.options(lcdFlags);uiDirty();}
void uiInfo(const String&text,uint32_t now){uiSetup(SetupStage::Connected,text,now);infoUntil=now+1800;}
void uiRoundBrightness(uint8_t value){
 roundBrightness=value;tft.startWrite();
 // GC9A01 datasheet 6.2.26/27: full manual brightness, dimming off, BL on.
 // This affects physical backlight only if the module wires LEDPWM to its driver.
 tft.writecommand(0x51);tft.writedata(value);tft.writecommand(0x53);tft.writedata(0x24);tft.endWrite();
}
void uiPrintStats(){Serial.printf("[ui] rotation=3 brightness_request=%u round_frames=%u lyric_frames=%u compose_tick_max_us=%u round_present_max_us=%u strip_present_max_us=%u target_frame_ms=40 workspace_bytes=%u skipped_rows=%u visual_mode=%u visual_style=%u lcd_flags=%u\n",roundBrightness,recordFrames,lyricFrames,composeMaxUs,roundPresentMaxUs,stripPresentMaxUs,unsigned(sizeof work),skippedRows,unsigned(visualMode),visualScene.style,lcdFlags);}
void uiBegin(){pinMode(LYRIC_CS,OUTPUT);digitalWrite(LYRIC_CS,HIGH);pinMode(ROUND_CS,OUTPUT);digitalWrite(ROUND_CS,HIGH);Serial.println("[display:init] GC9A01 SPI2 begin");tft.init();Serial.println("[display:init] GC9A01 ready");tft.setRotation(3);tft.setSwapBytes(true);uiRoundBrightness(255);Serial.println("[display:init] ST7789 begin");strip.begin();Serial.println("[display:init] ST7789 ready");bitmap.begin();quoteBitmap.begin();Serial.println("[display:init] font ready");tft.fillScreen(TFT_BLACK);baseDirty=true;}
void uiMenu(int selected,bool shown,bool effects){bool entering=shown&&(!menu||menuEffects!=effects||toneEditor);toneEditor=false;menuEffects=effects;int count=effects?vinyl::VISUAL_MENU_COUNT:vinyl::MAIN_MENU_COUNT;selection=(selected%count+count)%count;menu=shown;job=Job::Idle;stripY=76;labelY=56;if(shown){if(entering){menuOpened=uiNow;roundValid=false;}menuMotion.select(selection,uiNow,entering,count);menuDirty=true;menuSettling=true;}else uiRestore();}
void uiBassBoost(int value,bool available){
 value=std::max(0,std::min(3,value));if(toneEditor&&toneValue==value&&toneAvailable==available)return;
 if(!toneEditor)roundValid=false;if(job==Job::Lyric)force=true;job=Job::Idle;stripY=76;labelY=56;
 menu=true;toneEditor=toneDirty=true;toneValue=value;toneAvailable=available;
}
void uiVolume(int old,int value,bool hit,uint32_t now){popup.show(old,value,hit,value>=old?1:-1,now);ringDirty=true;}
void uiTransport(int direction,uint32_t now){transportDirection=direction<0?-1:1;transportAt=now;transportVisible=true;labelDirty=true;}
void uiUpdate(const char*message,unsigned percent){job=Job::Idle;roundValid=false;tft.fillScreen(BG);tft.setTextColor(CREAM,BG);tft.drawCentreString(message,120,80,2);tft.drawCentreString(String(percent)+"%",120,120,4);}
void uiConnectivity(bool connected){if(wifiReady!=connected){wifiReady=connected;wifiIconDirty=true;force=true;}}
void uiSetup(SetupStage stage,const String& msg,uint32_t now){
 ConnectionScene scene=stage==SetupStage::Boot?ConnectionScene::Boot:stage==SetupStage::Joining?ConnectionScene::Joining:stage==SetupStage::Portal?ConnectionScene::Portal:stage==SetupStage::Scanning?ConnectionScene::Scanning:stage==SetupStage::Failed?ConnectionScene::Failed:stage==SetupStage::Connected?ConnectionScene::Online:ConnectionScene::Audio;
 connectionFrame(scene,msg.c_str(),now);
}
void uiTest(){for(unsigned y=0;y<76;++y){uint16_t row[284];for(unsigned x=0;x<284;++x)row[x]=x<94?0xf800:x<189?0x07e0:0x001f;strip.row(0,y,284,row);}tft.fillScreen(TFT_RED);delay(500);tft.fillScreen(TFT_GREEN);delay(500);tft.fillScreen(TFT_BLUE);delay(500);uiRestore();}
void uiTick(uint32_t now){
 uiNow=now;recordMotion.tick(now,playClock.playing&&!menu&&!infoUntil);
 if(infoUntil){if(int32_t(now-infoUntil)<0)return;uiRestore();}
 ConnectionScene idle=!linkOnline?ConnectionScene::Audio:!(audioState.flags&linkwire::CONNECTED)?ConnectionScene::Pair:(!track.title[0]&&!playClock.playing)?ConnectionScene::Ready:ConnectionScene::None;
 if(!menu&&idle!=ConnectionScene::None){if(idle!=connectionScene||force||now-connectionAt>=160){connectionFrame(idle,"",now);force=false;}return;}
 if(!menu&&connectionScene!=ConnectionScene::None)uiRestore();
 if(popup.visible&&popup.tick(now))ringDirty=true;
 if(transportVisible){labelDirty=true;if(uint32_t(now-transportAt)>=650)transportVisible=false;}
 if(job!=Job::Idle&&jobGeneration!=track.generation){job=Job::Idle;stripY=76;labelY=56;force=true;labelDirty=true;}
 if(job!=Job::Idle){compose(now);return;}
 // Ring updates get first use of the shared workspace, without stopping motion.
 if(ringDirty||((popup.visible||ringWasVisible)&&uint32_t(now-ringAt)>=33)){volumeRing(now);}
 if(menu){if(toneEditor){if(toneDirty)toneFrame();}else if(menuDirty||(menuSettling&&now-menuAt>=40)){menuFrame(now);if(popup.visible)volumeRing(now);}}
 else if(baseDirty){job=Job::Base;composeY=0;jobGeneration=track.generation;compose(now);return;}
 else {if(progressDirty||now-progressAt>=250)progressRing(now);playbackIcon();wifiBadge();}
 if(menu&&idle!=ConnectionScene::None){if(idle!=connectionScene||force||now-connectionAt>=500){connectionFrame(idle,"",now,false);force=false;}return;}
 if(menu&&connectionScene!=ConnectionScene::None){connectionScene=ConnectionScene::None;force=true;}
 // A finished snapshot is sent as one burst; rendering remains cooperative.
 uint32_t interval=quoteMotion.active?40u:quoteView.kind==0?40u:90u;
 bool lyricDue=force||now-lastStrip>=interval;
 bool albumDue=!menu&&(playClock.playing||labelDirty)&&now-lastRound>=vinyl::RECORD_FRAME_MS;
 // Alternate if both are due, so a busy lyric animation cannot starve the disc.
 static bool preferAlbum=false;
 if(albumDue&&(!lyricDue||preferAlbum)){preferAlbum=false;lastRound=now;labelDirty=false;labelY=-56;float angle=recordMotion.angleRadians();recordTransform.set(lroundf(cosf(angle)*16384),lroundf(sinf(angle)*16384),vinyl::ART_SIZE);job=Job::Album;jobTransport=transportVisible;jobTransportAt=transportAt;jobDirection=transportDirection;}
 else if(lyricDue){preferAlbum=true;lastStrip=now;prepareStrip(now);if(stripFirst>stripLast)return;job=Job::Lyric;}
 else return;
 jobNow=now;composeY=job==Job::Lyric?stripFirst:0;jobGeneration=track.generation;compose(now);
}
