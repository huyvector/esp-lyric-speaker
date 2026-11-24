"""Run the real lookup worker with deterministic cache/network/queue doubles."""
from pathlib import Path
import ast,subprocess,tempfile
base=Path(__file__).resolve().parents[1];source=(base/'display/src/network.cpp').read_text()
tree=ast.parse((base/'test/catalog_match_test.py').read_text());stub=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stub' for t in n.targets))
stub=stub[:stub.index('struct Track{')].replace('for(auto&c:*this)c=char(std::tolower(uint8_t(c)));','for(size_t i=0;i<size();++i)(*this)[i]=char(std::tolower(uint8_t((*this)[i])));')
stub+=r'''
#include "runtime_budget.h"
#include "request_policy.h"
#include <vector>
#include <ctime>
struct Track{uint32_t generation=1,duration=180000;char title[192]="Song",artist[128]="Artist",album[128]="Album",localPath[128]{};};
struct Done{};Track input;bool delivered=false,valid=true;
namespace storage{struct Lease{};}

RuntimeBudget runtimeBudget;vinyl::RequestGate requestGate;
uint32_t lookupStartedAt=0;uint32_t requestRetryMs=30000;int lastHTTP=0;const char*requestFailure="ERROR";
int requests=0;constexpr int pdTRUE=1,portMAX_DELAY=0,SEARCH_INLINE=-2;
bool lrcHit=false,artHit=false,albumHit=false,getOK=true,pending=false,pendAfterCatalog=false;
int searchResult=-1,getCalls=0,searchCalls=0,catalogCalls=0,coverCalls=0;uint32_t retry=0;
struct {template<class...T>void printf(const char*,T...){}void println(const char*){}}Serial;
struct {unsigned getFreeHeap(){return 45000;}unsigned getMinFreeHeap(){return 20000;}}ESP;
struct {int status(){return 3;}}WiFi;constexpr int WL_CONNECTED=3;
uint32_t retryDelay(uint32_t wait){return wait+1000;}
uint32_t millis(){return 1000;}void vTaskDelay(int){}int pdMS_TO_TICKS(int i){return i;}int uxTaskGetStackHighWaterMark(void*){return 2200;}
size_t strlcpy(char*d,const char*s,size_t n){size_t l=strlen(s);if(n){size_t k=std::min(l,n-1);memcpy(d,s,k);d[k]=0;}return l;}
int xQueueReceive(int,Track*t,int){if(delivered)throw Done{};*t=input;delivered=true;return pdTRUE;}
unsigned uxQueueMessagesWaiting(int){return pending?1:0;}
bool current(const Track&){return valid;}String cleaned(const char*s){return s;}
String cacheStem(const Track&){return "/cache/song";}String albumCacheStem(const Track&){return "/cache/album";}
bool streamCache(const Track&,const String&p,bool art){return art?(p=="/cache/album.rgb"?albumHit:artHit):lrcHit;}
enum class ResultKind{Status};void publish(const Track&,ResultKind,const char*,uint32_t wait=0){retry=wait;}
bool fetchLyrics(const Track&,int,const String&){++getCalls;lastHTTP=getOK?200:503;if(!getOK){requestFailure="SERVICE BUSY";requestRetryMs=31000;}return getOK;}
int searchLyrics(const Track&,const String&){++searchCalls;lastHTTP=searchResult==SEARCH_INLINE?200:503;return searchResult;}
String findArtwork(const Track&,bool&match){++catalogCalls;match=true;if(pendAfterCatalog)pending=true;return "https://cover";}
bool fetchArtwork(const Track&,const String&,const String&,const String&){++coverCalls;return true;}
void reset(){input=Track{};requestGate=vinyl::RequestGate{};delivered=false;valid=true;lrcHit=artHit=albumHit=pending=pendAfterCatalog=false;getOK=true;searchResult=-1;getCalls=searchCalls=catalogCalls=coverCalls=0;retry=0;requestRetryMs=30000;}
'''
checks=r'''
void run(){try{networkTask(nullptr);}catch(Done&){}assert(!runtimeBudget.background.load());}
int main(){
 
 reset();lrcHit=artHit=true;run();assert(!getCalls&&!searchCalls&&!catalogCalls&&!coverCalls);
 reset();albumHit=true;run();assert(getCalls==1&&!catalogCalls&&!coverCalls);
 reset();artHit=true;requestGate.failed(vinyl::Service::LyricsGet,503,30,1000);searchResult=SEARCH_INLINE;run();assert(!getCalls&&searchCalls==1);
 reset();artHit=true;strcpy(input.artist,"Artist, Guest");requestGate.failed(vinyl::Service::LyricsSearch,503,30,1000);run();assert(getCalls==1&&!searchCalls);
 reset();requestGate.failed(vinyl::Service::LyricsGet,503,30,uint32_t(1000-28000));requestGate.failed(vinyl::Service::LyricsSearch,503,30,1000);run();assert(retry==3000&&!getCalls&&!searchCalls&&!catalogCalls);
 reset();getOK=false;searchResult=SEARCH_INLINE;run();assert(getCalls==1&&searchCalls==1&&catalogCalls==1&&coverCalls==1);
 reset();lrcHit=true;pending=true;run();assert(!catalogCalls&&!coverCalls);
 reset();lrcHit=true;pendAfterCatalog=true;run();assert(catalogCalls==1&&!coverCalls);
 reset();valid=false;run();assert(!getCalls&&!searchCalls&&!catalogCalls&&!coverCalls);
 std::cout<<"Actual worker: full cache, shared album art, available endpoint selection, earliest retry, pending lyrics priority, stale cancellation: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'worker.cpp').write_text(stub+source[source.index('void networkTask(void*)'):]+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'shared'),'-I'+str(base/'display/include'),str(p/'worker.cpp'),'-o',str(p/'worker')],check=True)
 subprocess.run([str(p/'worker')],check=True)
