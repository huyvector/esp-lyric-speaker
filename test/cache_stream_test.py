"""Exercise actual cache I/O and UI result handlers with a native filesystem.
No flashFS card needed; the ESP32 FS/queue doubles execute the real firmware paths.
"""
from pathlib import Path
import re,subprocess,tempfile
base=Path(__file__).resolve().parents[1]
network=(base/'display/src/network.cpp').read_text(); main=(base/'display/src/main.cpp').read_text(); app=(base/'display/include/app.h').read_text()

local_code='' 
cache=network[network.index('constexpr size_t CACHE_BLOCK'):network.index('struct StagingAllocator')]
handler=main[main.index('void handleResults()'):main.index('void controls(')]
types=app[app.index('enum class ResultKind'):app.index('extern std::atomic')]
stubs=r'''
#include "lyrics_core.h"
#include "player_controls.h"
vinyl::MemoryRetry memoryWait;
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
namespace sf=std::filesystem;
struct String:std::string {
 using std::string::string; String(const std::string&s):std::string(s){} String()=default;
 bool endsWith(const char*s)const {size_t n=strlen(s);return size()>=n&&compare(size()-n,n,s)==0;}
 String substring(size_t a,size_t b)const{return substr(a,b-a);}
 int lastIndexOf(char c)const{auto at=rfind(c);return at==npos?-1:int(at);}
 void remove(size_t at){erase(at);}
 size_t length()const{return size();} bool isEmpty()const{return empty();}
};
size_t maxRead=0,maxWrite=0;bool failWrites=false,reverseDirectory=false;int failReadCall=-1,readCalls=0;
struct File {
 struct Impl {
  std::fstream io;String path;std::vector<sf::path> entries;size_t index=0;bool dir=false;
  int* slots=nullptr;~Impl(){if(slots)--*slots;}
 };
 std::shared_ptr<Impl> p;
 explicit operator bool()const{return p&&(p->dir||p->io.is_open());}
 size_t size(){auto at=p->io.tellg();p->io.seekg(0,std::ios::end);auto n=p->io.tellg();p->io.seekg(at);return size_t(n);}
 size_t read(uint8_t*b,size_t n){maxRead=std::max(maxRead,n);if(++readCalls==failReadCall)return 0;p->io.read(reinterpret_cast<char*>(b),n);return size_t(p->io.gcount());}
 size_t write(const uint8_t*b,size_t n){maxWrite=std::max(maxWrite,n);if(failWrites)return 0;p->io.write(reinterpret_cast<const char*>(b),n);return p->io?n:0;}
 bool seek(size_t n){p->io.clear();p->io.seekg(n);return bool(p->io);}
 void close(){if(p&&p->io.is_open())p->io.close();p.reset();}
 String path(){return p?p->path:String();}
 File openNextFile();
};
struct FS {
 sf::path root;
 int limit=0,active=0,peak=0;
 File open(const String&path,const char*mode="r"){
  File f;auto real=root/path.substr(1);if(!sf::exists(real)&&strcmp(mode,"w"))return f;
  f.p=std::make_shared<File::Impl>();f.p->path=path;f.p->dir=sf::is_directory(real);
  if(f.p->dir){for(auto&e:sf::directory_iterator(real))f.p->entries.push_back(e.path());if(reverseDirectory)std::reverse(f.p->entries.begin(),f.p->entries.end());return f;}
  if(limit){assert(active<limit);f.p->slots=&active;++active;peak=std::max(peak,active);}
  f.p->io.open(real,std::ios::binary|(strcmp(mode,"w")==0?(std::ios::out|std::ios::trunc):std::ios::in));return f;
 }
 bool remove(const String&p){return sf::remove(root/p.substr(1));}
 bool rename(const String&a,const String&b){std::error_code ec;sf::rename(root/a.substr(1),root/b.substr(1),ec);return !ec;}
};
File File::openNextFile(){if(!p||p->index>=p->entries.size())return {};File f;f.p=std::make_shared<Impl>();f.p->path="/cache/"+p->entries[p->index++].filename().string();f.p->io.open(p->entries[p->index-1],std::ios::binary|std::ios::in);return f;}
#define FILE_READ "r"
#define FILE_WRITE "w"
struct SerialStub {template<class...T>void printf(const char*,T...){} void println(const char*){} } Serial;
void uiDirty(){}
void vTaskDelay(int){}
uint32_t millis(){return 1000;}
size_t strlcpy(char*d,const char*s,size_t n){size_t len=strlen(s);if(n){size_t k=std::min(len,n-1);memcpy(d,s,k);d[k]=0;}return len;}
struct Track{uint32_t generation=1;char localPath[128]{};};
std::atomic<uint32_t> generation{1};
bool current(const Track&t){return t.generation==generation.load();}
FS flashFS,internal;FS*cacheFS=nullptr;
constexpr uint32_t CACHE_MAGIC=0x31564C43;
struct Header{uint32_t magic,length,crc;};
alignas(4)uint8_t staging[vinyl::TRANSFER_LIMIT+1];
uint32_t lyricWords[vinyl::TEXT_LIMIT/4]{};vinyl::Cue cueWords[vinyl::CUE_LIMIT]{};
vinyl::PackedLyrics lyrics;vinyl::Clock playClock;
uint32_t artWords[vinyl::ART_BYTES/4]{};volatile uint32_t*artwork=artWords;
bool loadingLyrics=false,haveArtwork=false;int cacheLyricOffset=0;
char lyricStatus[40]{};uint32_t retryAt=0;unsigned retryCount=0;
int results=0,consumed=1;constexpr int pdTRUE=1;bool hasResult=false;
'''
queue=r'''
Result pending;int published=0,starts=0,ends=0;
int xQueueReceive(int,Result*r,int){if(!hasResult)return 0;*r=pending;hasResult=false;return pdTRUE;}
int xQueueSend(int,void*,int){return pdTRUE;}
'''
publish=r'''
void publish(const Track&t,ResultKind kind,const char*status,uint32_t delay=0,uint16_t length=0,uint16_t offset=0){
 if(!current(t))return;
 ++published;if(kind==ResultKind::LyricsStart||kind==ResultKind::ArtworkStart)++starts;
 if(kind==ResultKind::LyricsEnd||kind==ResultKind::ArtworkEnd)++ends;
 pending={t.generation,kind,"",delay,length,offset};strlcpy(pending.status,status,sizeof pending.status);hasResult=true;handleResults();
}
'''
checks=r'''
std::string load(const sf::path&p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 assert(argc==3);reverseDirectory=std::string(argv[2])=="reverse";flashFS.root=sf::path(argv[1])/"sd";internal.root=sf::path(argv[1])/"internal";
 flashFS.limit=2;
 sf::create_directories(flashFS.root/"cache");sf::create_directories(internal.root/"cache");cacheFS=&flashFS;
 lyrics.text.words=lyricWords;lyrics.cues.entries=cueWords;Track t;
 const char*lrc="[00:03.00]Tiếng Việt\n[00:01.00]Hello\n[offset:-500]";
 memcpy(staging,lrc,strlen(lrc));writeCache("/cache/song.lrc",strlen(lrc));
 assert(load(flashFS.root/"cache/song.lrc")==lrc);assert(load(flashFS.root/"cache/song.lrc.crc").size()==sizeof(Header));
 assert(streamCache(t,"/cache/song.lrc",false));assert(lyrics.count==2&&lyrics.cues[0].ms==500&&lyrics.cues[1].ms==2500&&!loadingLyrics);
 char text[64];lyrics.copyLine(1,text,sizeof text);assert(!strcmp(text,"Tiếng Việt"));
 for(size_t i=0;i<vinyl::ART_BYTES;++i)staging[i]=uint8_t(i*7);
 writeCache("/cache/song.rgb",vinyl::ART_BYTES);memset(artWords,0,sizeof artWords);
 assert(streamCache(t,"/cache/song.rgb",true)&&haveArtwork);
 for(size_t i=0;i<vinyl::ART_BYTES/4;++i){uint8_t b[4];for(int j=0;j<4;++j)b[j]=uint8_t((i*4+j)*7);assert(artWords[i]==vinyl::read32(b));}
 assert(maxRead<=512&&maxWrite<=512);
 // Reboot-like reload: only files, no in-memory lyrics/art, no network.
 lyrics.clear();haveArtwork=false;cacheFS=nullptr;cacheFS=&flashFS;
 assert(streamCache(t,"/cache/song.lrc",false)&&streamCache(t,"/cache/song.rgb",true));assert(lyrics.count==2&&haveArtwork);
 // A UTF-8 glyph crosses the 512-byte I/O boundary without being split
 // during line parse; line buffering is separate from the storage read block.
 std::string longText=std::string(501,'a')+"ế";
 std::string longLrc="[00:01.00]"+longText+"\n[00:02.00]Next\n";
 memcpy(staging,longLrc.data(),longLrc.size());writeCache("/cache/boundary.lrc",longLrc.size());
 assert(streamCache(t,"/cache/boundary.lrc",false)&&lyrics.count==2);
 char copied[600];lyrics.copyLine(0,copied,sizeof copied);assert(copied==longText);
 // CRC rejects tampered data before publishing any chunk.
 {std::fstream f(flashFS.root/"cache/song.rgb",std::ios::in|std::ios::out|std::ios::binary);f.put('!');}
 int before=published;assert(!streamCache(t,"/cache/song.rgb",true));assert(published==before);
 // A second-pass I/O failure aborts partial lyrics instead of displaying them.
 readCalls=0;failReadCall=3;assert(!streamCache(t,"/cache/song.lrc",false));assert(!loadingLyrics&&lyrics.count==0);failReadCall=-1;
 // Old releases prefixed the data with Header; retain compatibility.
 {std::ofstream f(flashFS.root/"cache/legacy.lrc",std::ios::binary);Header h{CACHE_MAGIC,uint32_t(strlen(lrc)),vinyl::crc32(reinterpret_cast<const uint8_t*>(lrc),strlen(lrc))};f.write(reinterpret_cast<char*>(&h),sizeof h);f<<lrc;}
 assert(streamCache(t,"/cache/legacy.lrc",false)&&lyrics.count==2);
 // 64px art from 0.4.1 must be rejected while the existing LRC still loads.
 {std::vector<uint8_t> old(64*64*2,0x5a);std::ofstream f(flashFS.root/"cache/old64.rgb",std::ios::binary);Header h{CACHE_MAGIC,uint32_t(old.size()),vinyl::crc32(old.data(),old.size())};f.write(reinterpret_cast<char*>(&h),sizeof h);f.write(reinterpret_cast<char*>(old.data()),old.size());}
 before=published;assert(!streamCache(t,"/cache/old64.rgb",true)&&published==before);assert(streamCache(t,"/cache/song.lrc",false));
 // Full-card writes do not replace the existing complete payload.
 failWrites=true;strcpy(reinterpret_cast<char*>(staging),"bad");writeCache("/cache/song.lrc",3);failWrites=false;
 assert(load(flashFS.root/"cache/song.lrc")==lrc);
 // Flash cache evicts entries when its asset budget is exceeded.
 for(int i=0;i<80;++i){std::ofstream(flashFS.root/"cache"/(std::to_string(i)+".lrc"))<<"x";std::ofstream(internal.root/"cache"/(std::to_string(i)+".lrc"))<<"x";}
 auto count=[](const sf::path&p){size_t n=0;for(auto&e:sf::directory_iterator(p))++n;return n;};
 size_t n=count(flashFS.root/"cache");pruneCache();assert(count(flashFS.root/"cache")<n);
 cacheFS=&internal;n=count(internal.root/"cache");pruneCache();assert(count(internal.root/"cache")<n);
 // Larger artwork must also obey the byte budget even below the asset count.
 for(int i=0;i<6;++i){std::ofstream f(internal.root/"cache"/("large"+std::to_string(i)+".rgb"),std::ios::binary);std::string payload(120*1024,'x');f<<payload;}
 pruneCache(vinyl::ART_BYTES);size_t cachedBytes=0;for(auto&e:sf::directory_iterator(internal.root/"cache")){auto name=e.path().filename().string();if(name.size()>4&&(name.substr(name.size()-4)==".rgb"||name.substr(name.size()-4)==".lrc"))cachedBytes+=sf::file_size(e.path())+sizeof(Header);}assert(cachedBytes+vinyl::ART_BYTES<=CACHE_BYTES);
 assert(flashFS.peak==2&&flashFS.active==0);
 // Eviction above may legitimately remove song.lrc: directory iteration order
 // differs between filesystems. Recreate the fixture for memory-wait recovery.
 cacheFS=&flashFS;memcpy(staging,lrc,strlen(lrc));writeCache("/cache/song.lrc",strlen(lrc));
 retryCount=2;for(unsigned i=0;i<10;++i){publish(t,ResultKind::Status,"LOW MEMORY",30000);assert(memoryWait.pending&&retryCount==2&&retryAt==0);}
 cacheFS=&flashFS;assert(streamCache(t,"/cache/song.lrc",false)&&!memoryWait.pending);generation=2;publish(t,ResultKind::Status,"LOW MEMORY",30000);assert(!memoryWait.pending);generation=1;
 assert(flashFS.active==0);
 std::cout<<"Cache bounded file slots / raw LRC / CRC / legacy / 512B chunks / UI handoff / reboot / I/O abort / full card / bounded flash retention: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as temp:
 src=Path(temp)/'cache.cpp';exe=Path(temp)/'cache';src.write_text(stubs+types+queue+handler+publish+cache+local_code+checks)
 subprocess.run(['c++','-std=c++17','-g','-fsanitize=address,undefined','-I'+str(base/'shared'),str(src),'-o',str(exe)],check=True)
 for order in ('native','reverse'):
  directory=Path(temp)/order;directory.mkdir()
  subprocess.run([str(exe),str(directory),order],check=True)
