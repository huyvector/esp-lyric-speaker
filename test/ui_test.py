"""Actual dual display renderer + real 16-bit U8g2, with SPI/display doubles.
Usage: python3 test/ui_test.py /path/to/U8g2/src/clib output-directory
"""
from pathlib import Path
import concurrent.futures,subprocess,sys
base=Path(__file__).resolve().parents[1];lib=Path(sys.argv[1]).resolve();build=Path(sys.argv[2]).resolve();build.mkdir(parents=True,exist_ok=True)
source=(base/'display/src/render_ui.cpp').read_text();source='\n'.join(l for l in source.splitlines() if not l.startswith('#include'))
s=(lib/'u8g2_fonts.c').read_text(encoding='latin1');start=s.index('const uint8_t u8g2_font_unifont_t_vietnamese1[');end=s.index('";',start)+2
font=build/'font.c';font.write_text('#include "u8g2.h"\n'+s[start:end]+'\n')
sources=[p for p in lib.glob('*.c') if 'fonts' not in p.name]+[font]
def compile(p):
 o=build/(p.stem+'.o')
 if not o.exists() or p.stat().st_mtime>o.stat().st_mtime:
  subprocess.run(['cc','-Os','-DU8G2_16BIT','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-I'+str(lib),'-c',str(p),'-o',str(o)],check=True,capture_output=True)
 return str(o)
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:objects=list(pool.map(compile,sources))
stub=r'''
#include "u8g2.h"
#include "lyrics_core.h"
#include "lyric_layout.h"
#include "quote_scene.h"
#include "artwork_render.h"
#include "record_animation.h"
#include "vinyl_skin.h"
#include "player_controls.h"
#include "link_protocol.h"
#include "pins.h"
#include "ui_canvas.h"
#include "ui_assets.h"
#include "visualizer.h"
#include "lcd_options.h"
#include "connection_icons.h"
#include "device_identity.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <string>
#include <cstdio>
#include <iostream>
#include <fstream>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define MSBFIRST 1
#define SPI_MODE0 0
#define TFT_BLACK 0
#define TFT_RED 0xf800
#define TFT_GREEN 0x07e0
#define TFT_BLUE 0x001f
struct{void println(const char*){}template<class...T>void printf(const char*,T...){}}Serial;
uint32_t fakeUs=0,clockStep=1;uint32_t micros(){return fakeUs+=clockStep;}
int pins[32]{};void digitalWrite(int p,int v){pins[p]=v;}void pinMode(int,int){}void delay(int){}
struct String:std::string{using std::string::string;String(unsigned i):std::string(std::to_string(i)){}String(const std::string&s):std::string(s){}};
struct SPISettings{SPISettings(int,int,int){}};
uint16_t wide[76][284]{};

struct SPIStub{bool selected=false;uint8_t op=0,inversion=0;unsigned x=0,y=0,w=0,end=0,rows=0,frames=0;
 void beginTransaction(SPISettings){assert(!selected);selected=true;}void endTransaction(){assert(selected&&pins[LYRIC_CS]==HIGH);selected=false;}
 void write(uint8_t v){assert(selected&&pins[ROUND_CS]==HIGH&&pins[LYRIC_CS]==LOW);if(pins[DISPLAY_DC]==LOW){op=v;if(op==0x20||op==0x21)inversion=op;if(op==0x2c)++frames;}}
 void writeBytes(const uint8_t*p,size_t n){assert(selected&&pins[DISPLAY_DC]);if(op==0x2a){assert(n==4);x=(p[0]*256+p[1])-18;w=p[2]*256+p[3]-18-x+1;assert(x+w<=284);}else if(op==0x2b){y=p[0]*256+p[1]-82;end=p[2]*256+p[3]-82;assert(y<=end&&end<76);}else if(op==0x2c){assert(n==w*2&&y<=end);for(unsigned i=0;i<w;++i)wide[y][x+i]=p[i*2]*256+p[i*2+1];++rows;++y;}}
}SPI;
struct TFT_eSPI{uint16_t pixels[240][240]{};unsigned batch=0,maxBatch=0,pushes=0,clears=0,wx=0,wy=0,ww=0,wh=0,at=0;bool writing=false;int rotation=0;uint8_t brightness=0,ctrl=0,lastCommand=0;
 void init(){}void setRotation(int r){rotation=r;}void setSwapBytes(bool){}
 void writecommand(uint8_t cmd){assert(writing);lastCommand=cmd;}void writedata(uint8_t v){assert(writing);if(lastCommand==0x51)brightness=v;else if(lastCommand==0x53)ctrl=v;else assert(false);}
 void fillRect(int x,int y,int w,int h,int c){for(int b=std::max(0,y);b<std::min(240,y+h);++b)for(int a=std::max(0,x);a<std::min(240,x+w);++a)pixels[b][a]=c;}
 void fillScreen(int c){++clears;fillRect(0,0,240,240,c);}void fillRoundRect(int x,int y,int w,int h,int,int c){fillRect(x,y,w,h,c);}
 void drawRoundRect(int,int,int,int,int,int){}void drawCircle(int,int,int,int){}void fillCircle(int,int,int,int){}
 void setTextFont(int){}void setTextSize(int){}void setTextColor(int,int=0){}void setCursor(int,int){}
 template<class T>void drawCentreString(const T&,int,int,int){}template<class...T>void printf(const char*,T...){}
 void startWrite(){assert(!writing);writing=true;batch=0;}void endWrite(){assert(writing);maxBatch=std::max(maxBatch,batch);writing=false;}
 void setAddrWindow(unsigned x,unsigned y,unsigned w,unsigned h){assert(writing&&x+w<=240&&y+h<=240&&w&&h);wx=x;wy=y;ww=w;wh=h;at=0;}
 void pushPixels(const void*ptr,unsigned n){assert(writing&&at+n<=ww*wh);auto p=static_cast<const uint16_t*>(ptr);for(unsigned i=0;i<n;++i,++at)pixels[wy+at/ww][wx+at%ww]=p[i];++pushes;}
 void pushImage(int x,int y,int w,int h,const uint16_t*p){assert(writing&&x>=0&&x+w<=240&&y>=0&&y+h<=240&&h>=1);for(int j=0;j<h;++j)for(int i=0;i<w;++i)pixels[y+j][x+i]=p[j*w+i];batch+=h;++pushes;}
};
enum class SetupStage{Boot,Joining,Portal,Scanning,Failed,Connected,Audio};
struct{uint32_t generation=1;char title[192]="Bài Ca Tuổi Trẻ",artist[128]="Da LAB";}track;
vinyl::Clock playClock;vinyl::PackedLyrics lyrics;uint32_t textWords[vinyl::TEXT_LIMIT/4]{},artworkWords[vinyl::ART_BYTES/4]{};vinyl::Cue cues[vinyl::CUE_LIMIT]{};
bool haveArtwork=true,loadingLyrics=false,linkOnline=true;char lyricStatus[40]="SYNCED";linkwire::StateData audioState;vinyl::VisualMode visualMode=vinyl::VisualMode::Auto;vinyl::OLEDMode oledMode=vinyl::OLEDMode::Lyric;int32_t offsetMs=0;
void uiMenu(int,bool,bool=false);
void uiDirty();void uiSetup(SetupStage,const String&,uint32_t);
'''
checks=r'''
void save(const std::string&path,const uint16_t*p,int w,int h){std::ofstream f(path,std::ios::binary);f<<"P6\n"<<w<<' '<<h<<"\n255\n";for(int i=0;i<w*h;++i){uint16_t c=p[i];uint8_t b[]={uint8_t((c>>11)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};f.write((char*)b,3);}}

void advance(unsigned from,unsigned until){for(unsigned t=from;t<until;t+=2)uiTick(t);}
int main(int argc,char**argv){assert(argc==2);std::string dir=argv[1];uiBegin();assert(tft.rotation==3&&tft.brightness==255&&tft.ctrl==0x24&&SPI.inversion==0x20);uiLcdOptions(4);assert(SPI.inversion==0x21);uiLcdOptions(0);assert(SPI.inversion==0x20);lyrics.text.words=textWords;lyrics.cues.entries=cues;
 for(int y=0;y<vinyl::ART_SIZE;++y)for(int x=0;x<vinyl::ART_SIZE;++x){uint16_t c=vinyl::rgb(10+x/3,24+y,70+x);int dx=x-48,dy=y-40;if(dx*dx+dy*dy<26*26)c=vinyl::rgb(240,190+x/3,70);if(y>67&&((x+y)/6)%2)c=0x196a;size_t at=y*vinyl::ART_SIZE+x;artworkWords[at/2]|=uint32_t(c)<<((at%2)*16);}
 assert(bitmap.width("0123456789012345678901234567890123456789")>255);
 assert(sizeof work==28800);lyrics.parse("[00:00.00]Một câu tiếng Việt rất dài cần ngắt dòng để dễ đọc trên màn hình mới\n[00:10.00]Câu tiếp theo\n");playClock.reset(0);playClock.duration=180000;playClock.setPlaying(true,0);audioState.flags=3;
 advance(0,1000);assert(recordFrames>=8&&layout.count>1&&!tft.writing&&!SPI.selected);
 for(int y=18;y<50;++y)for(int x=56;x<184;++x)assert(tft.pixels[y][x]!=CREAM&&tft.pixels[y][x]!=GOLD);
 save(dir+"/round.ppm",&tft.pixels[0][0],240,240);save(dir+"/lyrics.ppm",&wide[0][0],284,76);
 // Both compose jobs must be invisible until the complete sprite/frame exists.
 job=Job::Album;composeY=0;recordTransform.set(16384,0,vinyl::ART_SIZE);auto n=tft.pushes;clockStep=2100;compose(1000);assert(composeY==2&&job==Job::Album&&tft.pushes==n);clockStep=1;
 while(job!=Job::Idle){auto before=tft.pushes;compose(1000);if(job!=Job::Idle)assert(tft.pushes==before);}assert(tft.pushes==n+1);
 force=true;stripValid=false;prepareStrip(1000);job=Job::Lyric;composeY=stripFirst;n=SPI.rows;compose(1000);assert(job==Job::Lyric&&SPI.rows==n);
 while(job!=Job::Idle){compose(1000);if(job!=Job::Idle)assert(SPI.rows==n);}assert(SPI.rows>n);
 playClock.setPlaying(false,1000);advance(1000,1600);n=tft.pushes;auto frozen=recordMotion.phase;advance(1600,2000);assert(tft.pushes==n&&recordMotion.phase==frozen);
 // A missed final animation frame still gets painted before going idle.
 force=false;quoteMotion.start(1000);prepareStrip(2000);assert(stripFirst==18&&!quoteMotion.active&&frameIn[0]==256);prepareStrip(2000);assert(stripFirst>stripLast);
 ++track.generation;strcpy(track.title,"Một tên bài hát tiếng Việt thật dài để kiểm tra giới hạn");uiDirty();advance(2000,2400);assert(tft.pushes>n);
 // Volume belongs to the outer annulus. Music keeps moving behind it.
 playClock.setPlaying(true,2400);auto phase=recordMotion.phase,frames=recordFrames,clears=tft.clears;uiVolume(40,90,false,2400);advance(2400,2750);assert(recordMotion.phase!=phase&&recordFrames>frames&&tft.clears==clears);save(dir+"/volume.ppm",&tft.pixels[0][0],240,240);
 job=Job::Idle;popup.visible=false;popup.show(0,0,false,-1,2800);volumeRing(3000);unsigned low=0,high=0;for(const auto& span:vinyl::RIM_SPANS)for(unsigned x=span.x;x<span.x+span.count;++x)low+=tft.pixels[span.y][x]==GOLD;
 popup.visible=false;popup.show(127,127,false,1,3000);volumeRing(3200);for(const auto& span:vinyl::RIM_SPANS)for(unsigned x=span.x;x<span.x+span.count;++x)high+=tft.pixels[span.y][x]==GOLD;assert(low==0&&high>2500);
 popup.visible=false;volumeRing(3300);for(const auto& span:vinyl::RIM_SPANS)for(unsigned x=span.x;x<span.x+span.count;++x)assert(tft.pixels[span.y][x]==0);
 // Progress and volume occupy disjoint pixels, including AA fringes.
 uint8_t mask[240][240]{};for(const auto& span:vinyl::RIM_SPANS)for(unsigned x=span.x;x<span.x+span.count;++x)mask[span.y][x]=1;
 for(const auto& span:vinyl::PLAY_RING_SPANS)for(unsigned x=span.x;x<span.x+span.count;++x)assert(!mask[span.y][x]);
 playClock.setPlaying(false,3300);playClock.duration=10000;playClock.base=5000;playClock.tick=3300;progressDirty=true;progressRing(3300);
 uint32_t timeHash=0;for(const auto& span:vinyl::PLAY_RING_SPANS)for(unsigned x=span.x;x<span.x+span.count;++x)timeHash=timeHash*31+tft.pixels[span.y][x];
 popup.show(50,80,false,1,3300);volumeRing(3500);uint32_t afterHash=0;for(const auto& span:vinyl::PLAY_RING_SPANS)for(unsigned x=span.x;x<span.x+span.count;++x)afterHash=afterHash*31+tft.pixels[span.y][x];assert(timeHash==afterHash);save(dir+"/dual-rings.ppm",&tft.pixels[0][0],240,240);
 popup.visible=false;volumeRing(3500);playClock.duration=0;progressDirty=true;progressRing(3500);playClock.duration=180000;playClock.base=2000;playClock.tick=3500;
 // Curved carousel: all eight icons, wrap 7->0 follows shortest path, no clears.
 for(unsigned item=0;item<8;++item){uiMenu(item,true);unsigned start=3400+item*400;advance(start,start+380);save(dir+"/menu-"+std::to_string(item)+".ppm",&tft.pixels[0][0],240,240);}assert(tft.clears==clears);
 uiMenu(0,true);assert(std::abs(menuMotion.to-menuMotion.from)<=1024);for(unsigned t=6600;t<6900;t+=20){uiTick(t);save(dir+"/menu-anim-"+std::to_string(t)+".ppm",&tft.pixels[0][0],240,240);}uiMenu(0,false);advance(6900,7200);
 oledMode=vinyl::OLEDMode::Auto;lyrics.clear();audioState.peak=16000;playClock.setPlaying(true,7200);uiDirty();advance(7200,7900);assert(quoteView.kind==0);save(dir+"/visual-auto.ppm",&wide[0][0],284,76);
 oledMode=vinyl::OLEDMode::Lyric;lyrics.parse("[00:00.00]Tuổi trẻ ơi\n[00:02.00]Một thời thanh xuân\n[00:04.00]Đây là một câu dài hơn cần chia dòng để vẫn đọc rõ từng chữ\n[00:08.00]Câu mới đến rồi\n");++track.generation;strcpy(track.title,"Bài Ca Tuổi Trẻ");playClock.reset(8000);playClock.duration=180000;playClock.setPlaying(true,8000);uiDirty();advance(8000,8600);assert(quoteView.scale==512&&quoteEffect==0&&!quoteMotion.active);save(dir+"/quote-large.ppm",&wide[0][0],284,76);
 for(unsigned t=8600;t<10600;t+=2){unsigned frames=lyricFrames;uiTick(t);if(lyricFrames!=frames&&t>=9970){save(dir+"/lyric-anim-"+std::to_string(t)+".ppm",&wide[0][0],284,76);}}assert(quoteView.scale==384&&quoteEffect==1&&!quoteMotion.active);save(dir+"/quote-medium.ppm",&wide[0][0],284,76);
 advance(10600,12800);assert(quoteView.scale>=256&&quoteView.scale<=320&&quoteEffect==2);save(dir+"/quote-long.ppm",&wide[0][0],284,76);
 advance(12800,16600);assert(quoteEffect==3);save(dir+"/quote-glide.ppm",&wide[0][0],284,76);
 // Transport overlays are sprites, not text; they expire even while paused.
 uiTransport(1,16600);advance(16600,16900);save(dir+"/next.ppm",&tft.pixels[0][0],240,240);assert(transportVisible);
 uiTransport(-1,16900);advance(16900,17200);save(dir+"/previous.ppm",&tft.pixels[0][0],240,240);playClock.setPlaying(false,17200);advance(17200,17800);assert(!transportVisible);save(dir+"/paused.ppm",&tft.pixels[0][0],240,240);
 // Every fixed style plus auto can be selected; header/clock stay present.
 oledMode=vinyl::OLEDMode::VU;playClock.setPlaying(true,17800);
 for(unsigned effect=0;effect<5;++effect){visualMode=vinyl::VisualMode(effect);uiDirty();unsigned start=17800+effect*600;advance(start,start+580);assert(quoteView.kind==0);if(effect)assert(visualScene.style==effect-1);save(dir+"/visual-"+std::to_string(effect)+".ppm",&wide[0][0],284,76);}
 for(unsigned item=0;item<6;++item){uiMenu(item,true,true);unsigned start=21000+item*400;advance(start,start+380);save(dir+"/effect-menu-"+std::to_string(item)+".ppm",&tft.pixels[0][0],240,240);}
 uiMenu(0,false);advance(23400,23800);
 // Thin duration bar uses only bottom rows74/75, never time glyphs.
 job=Job::Idle;playClock.setPlaying(false,23800);playClock.duration=10000;playClock.base=5000;playClock.tick=23800;force=true;prepareStrip(23800);assert(quoteView.trackProgress==134);
 for(int y=72;y<76;++y)for(int x=0;x<284;++x){uint16_t c=stripPixel(x,y);if(y>=74&&x>=8&&x<142)assert(c==GOLD);else if(y>=74&&x>=142&&x<276)assert(c==0x1924);else assert(c==vinyl::rgb(7+y/28,10+y/24,16+y/20));}
 playClock.base=10000;prepareStrip(23800);assert(quoteView.trackProgress==268);playClock.duration=0;prepareStrip(23800);assert(quoteView.trackProgress==0);
 // EQ editor reuses the same packed workspace and preserves lyric playback.
 for(int value=0;value<=3;++value){uiBassBoost(value,true);uiTick(24000+value*100);while(job!=Job::Idle)uiTick(uiNow+2);uiTick(uiNow+2);assert(toneEditor&&toneValue==value);save(dir+"/bass-"+std::to_string(value)+".ppm",&tft.pixels[0][0],240,240);}
 uiBassBoost(0,false);uiTick(24500);save(dir+"/bass-offline.ppm",&tft.pixels[0][0],240,240);uiMenu(7,true);assert(!toneEditor);uiTick(24600);uiMenu(8,false);advance(24700,25100);
 // Mid-composition track change discards stale pixels, using the same workspace.
 job=Job::Album;composeY=3;jobGeneration=track.generation;++track.generation;uiTick(25100);assert(jobGeneration==track.generation||job==Job::Idle);
 uiRoundBrightness(0);assert(tft.brightness==0&&tft.ctrl==0x24);uiRoundBrightness(255);assert(tft.brightness==255&&tft.ctrl==0x24);
 auto pair=[&](const std::string& name){if(connectionScene!=ConnectionScene::None)for(int y=0;y<240;++y)for(int x=0;x<240;++x){int dx=2*x-239,dy=2*y-239;if(dx*dx+dy*dy>240*240)assert(tft.pixels[y][x]==0);}
 save(dir+"/"+name+"-round.ppm",&tft.pixels[0][0],240,240);save(dir+"/"+name+"-strip.ppm",&wide[0][0],284,76);};
 for(auto stage:{SetupStage::Boot,SetupStage::Joining,SetupStage::Portal,SetupStage::Scanning,SetupStage::Failed,SetupStage::Connected}){const char* name=stage==SetupStage::Boot?"boot":stage==SetupStage::Joining?"joining":stage==SetupStage::Portal?"portal":stage==SetupStage::Scanning?"scanning":stage==SetupStage::Failed?"failed":"online";uiSetup(stage,stage==SetupStage::Joining?"Home WiFi":stage==SetupStage::Failed?"Check the password":"192.168.2.132",26000);pair(name);}
 uiRestore();linkOnline=false;uiTick(27000);assert(connectionScene==ConnectionScene::Audio);pair("audio-offline");linkOnline=true;audioState.flags=0;uiConnectivity(true);uiTick(27200);assert(connectionScene==ConnectionScene::Pair);pair("pairing");strcpy(track.title,"");playClock.setPlaying(false,27400);audioState.flags=linkwire::CONNECTED;uiTick(27400);assert(connectionScene==ConnectionScene::Ready);pair("ready");
 uiMenu(7,true);uiTick(27600);auto statusBefore=tft.pushes;uiTick(27800);assert(menu&&connectionScene==ConnectionScene::Ready);auto oldRound=tft.pushes,oldStrip=SPI.frames;connectionFrame(ConnectionScene::Ready,"",27900,false);assert(tft.pushes==oldRound&&SPI.frames==oldStrip+1);uiMenu(8,false);strcpy(track.title,"Bài Ca Tuổi Trẻ");uiConnectivity(false);uiTick(28000);advance(28002,28400);assert(connectionScene==ConnectionScene::None&&!baseDirty);pair("music-offline");
 std::cout<<"Actual renderer + real Vietnamese font: normal/inverse controller commands, title + 2px duration bar, 54px body, disjoint progress/volume rims, no round text, next/prev popup expiry while paused, selectable visual scenes,  shared workspace28800, atomic album/lyric snapshots, 2ms compose checkpoint, dirty-row transfers, rim mute/max/erase, moving disc under volume, nine-item English menu/Bass Boost/connection scenes/no clears, four lyric effects/UTF8/layout, cancellation, SPI bounds/CS, rotation/brightness: PASS\n";
}

'''
file=build/'ui.cpp';file.write_text(stub+source+checks)
link=['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections']
subprocess.run(['c++','-std=c++17','-DU8G2_16BIT','-Os','-fsanitize=address,undefined',*link,'-I'+str(lib),'-I'+str(base/'shared'),'-I'+str(base/'display/include'),str(file),*objects,'-o',str(build/'ui')],check=True)
subprocess.run([str(build/'ui'),str(build)],check=True)
