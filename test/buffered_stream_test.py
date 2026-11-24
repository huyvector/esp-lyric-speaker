"""Exercise the actual bounded TLS->JSON stream, using a deterministic Client."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];source=(base/'display/src/network.cpp').read_text();code=source[source.index('class LimitedStream'):source.index('int nextSeparator')]
stub=r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <cassert>
#include <iostream>
constexpr size_t MAX_BODY=4096;uint32_t now=100;unsigned yields=0;bool valid=true;
struct Track{};bool current(const Track&){return valid;}uint32_t millis(){return now;}void vTaskDelay(int){++yields;}
struct Stream{virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;virtual size_t write(uint8_t)=0;void setTimeout(unsigned){};};
struct Client {
 std::string data;size_t at=0,block=512,reads=0,maxRead=0;bool stall=false;
 int available(){return stall?0:int(data.size()-at);}
 int read(uint8_t*dst,size_t n){assert(n<=512);maxRead=std::max(maxRead,n);++reads;n=std::min(n,std::min(block,data.size()-at));memcpy(dst,data.data()+at,n);at+=n;return int(n);}
};
'''
checks=r'''
int main(){
 Track t;
 for(size_t block:{1u,7u,511u,512u,4096u}){
  Client client;client.block=block;
  for(unsigned i=0;i<700;++i)client.data+="Tiếng Việt\n";
  LimitedStream stream(client,t);std::string actual;
  int first=stream.peek();assert(first==uint8_t(client.data[0])&&stream.peek()==first);
  while(stream.alive()){int c=stream.read();if(c<0)break;actual+=char(c);}
  assert(actual==client.data.substr(0,MAX_BODY));assert(!stream.alive()&&stream.available()==0&&stream.peek()==-1&&stream.read()==-1);assert(client.at==MAX_BODY);
  if(block>=512)assert(client.reads==8); // 4096 TLS reads -> 8 bulk reads.
 }
 Client client;client.data="0123456789";client.stall=true;LimitedStream stream(client,t);assert(stream.read()==-1);client.stall=false;assert(stream.read()=='0');
 valid=false;assert(stream.read()==-1&&stream.available()==0);valid=true;
 now+=18001;assert(stream.read()==-1);
 now=UINT32_MAX-99;Client wrap;wrap.data="hello";LimitedStream rollover(wrap,t);now+=18000;assert(!rollover.alive());
 std::cout<<"Actual TLS body stream: <=512-byte bulk reads, UTF-8 exact, short reads/stall/peek/cancel/size/deadline/rollover: PASS\n";
}
'''
# Publishing must release the LRC to the UI before optional SD commits.
for name,end in [('searchLyrics','bool fetchLyrics'),('fetchLyrics','String findArtwork')]:
 region=source[source.index(('int ' if name=='searchLyrics' else 'bool ')+name):source.index(end)]
 assert region.index('publish(t, ResultKind::Lyrics')<region.index('writeCache(path, length)')
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'stream.cpp').write_text(stub+code+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'stream.cpp'),'-o',str(p/'stream')],check=True)
 subprocess.run([str(p/'stream')],check=True)
