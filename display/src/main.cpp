#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include "app.h"
#include "render_ui.h"
#include "setup_display.h"
#include "wifi_setup.h"
#include "serial_link.h"
#include "controller.h"
#include "pins.h"
#include "encoder_core.h"
#include "player_controls.h"
#include "lcd_options.h"
std::atomic<uint32_t> generation{1};RuntimeBudget runtimeBudget;
QueueHandle_t requests=nullptr,results=nullptr,consumed=nullptr;fs::FS*cacheFS=nullptr;
alignas(4)uint8_t staging[vinyl::TRANSFER_LIMIT+1]{};
Track track;vinyl::PackedLyrics lyrics;vinyl::Clock playClock;
uint32_t lyricWords[vinyl::TEXT_LIMIT/4]{},artworkWords[vinyl::ART_BYTES/4]{};vinyl::Cue cueWords[vinyl::CUE_LIMIT]{};
volatile uint32_t*artwork=artworkWords;
bool haveArtwork=false,loadingLyrics=false,linkOnline=false;
char lyricStatus[40]="WAITING FOR AUDIO ESP32";
linkwire::StateData audioState;vinyl::OLEDMode oledMode=vinyl::OLEDMode::Auto;vinyl::VisualMode visualMode=vinyl::VisualMode::Auto;int32_t offsetMs=-120;
Preferences prefs;HardwareSerial port(1);SerialLink wire(port);uint32_t session=0,remoteSession=0,remoteGeneration=0,lastSnapshot=0;
struct Queued{uint8_t code=0;int value=0;};Queued fifo[8];unsigned first=0,count=0;struct {bool active=false;uint16_t seq=0;uint8_t data[9]{};uint32_t sent=0;unsigned retries=0;}pending;
struct {bool active=false,received=false;uint16_t seq=0;uint8_t type=0,status=255;uint32_t offset=0;}relay;
uint32_t lastAudio=0,changedAt=0,retryAt=0,volumeIntentAt=0;int cacheLyricOffset=0;unsigned retryCount=0;bool dirty=false,ready=false;vinyl::MemoryRetry memoryWait;
std::atomic<int> rotations{0};bool inverted=false,menu=false,effectMenu=false;int selected=0,desiredVolume=55,toneEdit=0,desiredBass=2;uint32_t toneIntentAt=0;
void encoderTask(void*){vinyl::Rotary decoder;decoder.previous=(digitalRead(ENC_CLK)<<1)|digitalRead(ENC_DT);for(;;){int n=decoder.update((digitalRead(ENC_CLK)<<1)|digitalRead(ENC_DT));if(n)rotations.fetch_add(n);vTaskDelay(1);}}
void command(uint8_t code,int value){if(code==linkwire::Volume||code==linkwire::Bass){for(unsigned n=0;n<count;++n){auto&q=fifo[(first+n)%8];if(q.code==code){q.value=value;return;}}}if(count==8){Serial.println("[link] command queue full");return;}fifo[(first+count)%8].code=code;fifo[(first+count)%8].value=value;++count;}
bool toneInFlight(){
 if(pending.active&&(pending.data[4]==linkwire::Bass))return true;
 for(unsigned n=0;n<count;++n){auto code=fifo[(first+n)%8].code;if(code==linkwire::Bass)return true;}
 return false;
}
void networkResume(){dirty=track.title[0];changedAt=millis();}
void networkCancel(){++generation;track.generation=generation;dirty=false;memoryWait.cancel();}
void clearTrack(){networkCancel();lyrics.clear();loadingLyrics=haveArtwork=false;retryCount=retryAt=0;uiDirty();strlcpy(lyricStatus,"LOOKING UP LYRICS",sizeof lyricStatus);}
void requestSnapshot(uint32_t now){if(now-lastSnapshot>500){lastSnapshot=now;command(linkwire::Snapshot);}}
void receiveFrame(const linkwire::Frame&f){uint32_t now=millis();
 if(f.type==linkwire::Ack&&f.len==6){if(relay.active&&f.seq==relay.seq&&f.data[0]==relay.type){relay.received=true;relay.status=f.data[1];relay.offset=linkwire::get32(f.data+2);}else if(pending.active&&f.seq==pending.seq&&f.data[0]==linkwire::Command){pending.active=false;if(f.data[1])Serial.printf("[link] command rejected=%u\n",f.data[1]);}return;}
 if(f.type==linkwire::Hello&&f.len==5){uint32_t id=linkwire::get32(f.data);if(id!=remoteSession){remoteSession=id;remoteGeneration=0;pending.active=false;first=count=0;track=Track{};clearTrack();playClock.reset(now);requestSnapshot(now);}return;}
 if(f.type==linkwire::Track&&f.len==460){uint32_t id=linkwire::get32(f.data),gen=linkwire::get32(f.data+4);if(id!=remoteSession)return;
  Track incoming;incoming.duration=linkwire::get32(f.data+8);memcpy(incoming.title,f.data+12,192);incoming.title[191]=0;memcpy(incoming.artist,f.data+204,128);incoming.artist[127]=0;memcpy(incoming.album,f.data+332,128);incoming.album[127]=0;
  if(remoteGeneration==gen&&!strcmp(incoming.title,track.title)&&!strcmp(incoming.artist,track.artist)&&!strcmp(incoming.album,track.album)&&incoming.duration==track.duration)return;
  remoteGeneration=gen;track=incoming;clearTrack();track.generation=generation;playClock.reset(now);playClock.duration=track.duration;dirty=track.title[0];changedAt=now;if(!dirty)strlcpy(lyricStatus,"PAIR IPHONE",sizeof lyricStatus);Serial.printf("[track] gen=%u remote=%u %s / %s\n",generation.load(),gen,track.title,track.artist);return;
 }
 if(f.type==linkwire::State){linkwire::StateData state;if(!linkwire::unpackState(f,state)||state.session!=remoteSession)return;lastAudio=now;linkOnline=true;audioState=state;
  if(now-volumeIntentAt>500)desiredVolume=state.volume;
  if(now-toneIntentAt>500&&!toneInFlight()){desiredBass=state.bass;}
  if(remoteGeneration!=state.generation){if(track.title[0]){track=Track{};clearTrack();playClock.reset(now);}requestSnapshot(now);return;}
  playClock.duration=state.duration;playClock.base=state.position;playClock.tick=now;playClock.playing=state.flags&linkwire::PLAYING;playClock.authoritative=state.flags&linkwire::AUTHORITATIVE;
 }
}
void transportTick(){unsigned budget=12;while(budget--&&wire.receive())receiveFrame(wire.frame);uint32_t now=millis();static uint32_t helloAt=0;if(now-helloAt>1500){helloAt=now;uint8_t p[5];linkwire::put32(p,session);p[4]=1;wire.send(linkwire::Hello,p,sizeof p);}
 if(linkOnline&&now-lastAudio>3000){linkOnline=false;playClock.setPlaying(false,now);strlcpy(lyricStatus,"AUDIO LINK LOST",sizeof lyricStatus);uiDirty();}
 if(relay.active)return;
 if(pending.active){if(now-pending.sent>500){if(pending.retries++>=3){pending.active=false;Serial.println("[link] command timeout");}else if(wire.send(linkwire::Command,pending.data,9,pending.seq))pending.sent=now;}return;}
 if(count&&remoteSession){auto q=fifo[first];first=(first+1)%8;--count;linkwire::put32(pending.data,session);pending.data[4]=q.code;linkwire::put32(pending.data+5,uint32_t(q.value));pending.seq=wire.next();pending.retries=0;pending.sent=now;pending.active=true;wire.send(linkwire::Command,pending.data,9,pending.seq);}
}
bool relayRequest(uint8_t type,const uint8_t*data,size_t n,uint32_t*offset){relay.active=true;relay.received=false;relay.type=type;relay.seq=wire.next();for(unsigned attempt=0;attempt<4;++attempt){wire.send(type,data,n,relay.seq);uint32_t since=millis();while(millis()-since<2000){transportTick();if(relay.received){relay.active=false;if(offset)*offset=relay.offset;return relay.status==0;}delay(1);}}relay.active=false;return false;}
void handleResults() {
  Result result;
  if (xQueueReceive(results, &result, 0) != pdTRUE) return;
  if (result.generation == generation.load()) {
    if (result.kind == ResultKind::Lyrics) { memoryWait.cancel(); loadingLyrics = false; lyrics.parse(reinterpret_cast<char*>(staging)); strlcpy(lyricStatus, result.status, sizeof lyricStatus); retryAt = 0;
      Serial.printf("[lyrics] gen=%u lines=%u bytes=%u status=%s authoritative=%u\n", result.generation, unsigned(lyrics.count), unsigned(lyrics.used), result.status, playClock.authoritative);
    }
    if (result.kind == ResultKind::LyricsStart) {
      lyrics.clear(); loadingLyrics = true; cacheLyricOffset = 0;
      strlcpy(lyricStatus, "READING CACHE", sizeof lyricStatus);
    }
    if (result.kind == ResultKind::LyricsLine && loadingLyrics) {
      const char* line = reinterpret_cast<char*>(staging);
      const char* off = strstr(line, "[offset:");
      if (off) cacheLyricOffset = std::max(-60000, std::min(60000, atoi(off + 8)));
      lyrics.append(line);
    }
    if (result.kind == ResultKind::LyricsEnd && loadingLyrics) {
      memoryWait.cancel(); lyrics.finish(cacheLyricOffset); loadingLyrics = false; retryAt = 0;
      strlcpy(lyricStatus, result.status, sizeof lyricStatus);
      Serial.printf("[lyrics] gen=%u cache=stream lines=%u bytes=%u\n", result.generation, unsigned(lyrics.count), unsigned(lyrics.used));
    }
    if (result.kind == ResultKind::LyricsAbort) { lyrics.clear(); loadingLyrics = false; }
    if (result.kind == ResultKind::ArtworkStart || result.kind == ResultKind::ArtworkAbort) haveArtwork = false;
    if (result.kind == ResultKind::ArtworkChunk && artwork && result.offset + result.length <= vinyl::ART_BYTES && result.length % 4 == 0 && result.offset % 4 == 0) {
      for (size_t i = 0; i < result.length / 4; ++i) artwork[result.offset / 4 + i] = vinyl::read32(staging + i * 4);
    }
    if (result.kind == ResultKind::ArtworkEnd) haveArtwork = true;
    if (result.kind == ResultKind::Artwork && artwork) {
      for (size_t i = 0; i < vinyl::ART_BYTES / 4; ++i) artwork[i] = vinyl::read32(staging + i * 4);
      haveArtwork = true;
    }
    if (result.kind == ResultKind::Status) {
      if (loadingLyrics) { lyrics.clear(); loadingLyrics = false; }
      strlcpy(lyricStatus, result.status, sizeof lyricStatus);
      if(strstr(result.status,"MEMORY")){memoryWait.arm(generation.load(),millis());retryAt=0;Serial.println("[lookup] waiting for TLS memory; retry budget preserved");}
      else if (retryCount < 2 && (strstr(result.status, "ERROR") || strstr(result.status, "NO WIFI") || strstr(result.status, "CLOCK") || strstr(result.status, "BUSY") || strstr(result.status, "MEMORY"))) {
        uint32_t wait = std::max<uint32_t>(1000u, result.retryAfterMs ? result.retryAfterMs : 30000u);
        retryAt = millis() + wait;
        Serial.printf("[lookup] retry=%u in=%ums status=%s\n", retryCount + 1, unsigned(wait), result.status);
      }
    }
  }
  uiDirty();
  uint8_t ack = 1; xQueueSend(consumed, &ack, 0);
}
void controls(uint32_t now){
 static vinyl::Key key;static bool gesture=false;static uint32_t clickAt=0,lastCue=0;static unsigned clicks=0;
 bool down=digitalRead(ENC_SW)==LOW;auto action=key.tick(down,now,false);int steps=rotations.exchange(0);if(inverted)steps=-steps;steps=constrain(steps,-20,20);
 if(steps){
  if(menu&&toneEdit){
   bool available=linkOnline;
   if(available){int& value=desiredBass;int target=constrain(value+steps,0,3);
    if(target!=value){value=target;toneIntentAt=now;command(linkwire::Bass,value);}
   }
  }
  else if(menu){int count=effectMenu?vinyl::VISUAL_MENU_COUNT:vinyl::MAIN_MENU_COUNT;selected=(selected+steps%count+count)%count;uiMenu(selected,true,effectMenu);}
  else if(down){gesture=true;clickAt=clicks=0;command(steps>0?linkwire::Next:linkwire::Previous);uiTransport(steps>0?1:-1,now);}
  else {int old=desiredVolume;desiredVolume=constrain(old+steps*3,0,127);volumeIntentAt=now;command(linkwire::Volume,desiredVolume);bool limit=desiredVolume==0||desiredVolume==127;if(limit&&now-lastCue>800){lastCue=now;command(linkwire::Cue);}uiVolume(old,desiredVolume,limit,now);}
 }
 if(action==vinyl::KeyAction::Hold&&!gesture){
  clickAt=clicks=0;
  if(menu&&toneEdit){toneEdit=0;uiMenu(selected,true);}
  else if(menu&&effectMenu){effectMenu=false;selected=3;uiMenu(selected,true);}
  else {menu=!menu;effectMenu=false;uiMenu(selected,menu);}
 }
 if(action==vinyl::KeyAction::Click){
  if(gesture){gesture=false;return;}
  if(menu){
   clickAt=clicks=0;
   if(toneEdit){toneEdit=0;uiMenu(selected,true);}
   else if(effectMenu){
    if(selected==5){effectMenu=false;selected=3;uiMenu(selected,true);}
    else {visualMode=vinyl::VisualMode(selected);prefs.putUChar("visual",unsigned(visualMode));oledMode=vinyl::OLEDMode::VU;prefs.putUChar("mode",unsigned(oledMode));menu=effectMenu=false;selected=3;uiMenu(selected,false);uiDirty();}
   }else if(selected==7){toneEdit=1;uiBassBoost(desiredBass,linkOnline);}
   else if(selected==3){effectMenu=true;selected=int(visualMode);uiMenu(selected,true,true);}
   else {menu=false;uiMenu(selected,false);switch(selected){
    case 0:command(linkwire::Next);uiTransport(1,now);break;
    case 1:command(linkwire::Previous);uiTransport(-1,now);break;
    case 2:case 4:oledMode=selected==2?vinyl::OLEDMode::Auto:vinyl::OLEDMode::Lyric;prefs.putUChar("mode",unsigned(oledMode));uiDirty();break;
    case 5:prefs.putBool("setup",true);ESP.restart();break;
    case 6:uiInfo(WiFi.localIP().toString(),now);break;
    default:break;
   }}
  }else {
   ++clicks;clickAt=now;
   if(clicks==3){command(linkwire::Previous);uiTransport(-1,now);clickAt=clicks=0;}
  }
 }
 if(toneEdit)uiBassBoost(desiredBass,linkOnline);
 if(!key.down&&!down&&gesture)gesture=false;
 // Resolve a double click only after the third-click window closes. A held
 // switch postpones resolution so a hold/press-turn can cancel pending clicks.
 if(clicks&&!down&&!key.down&&uint32_t(now-clickAt)>=300){
  if(clicks==2){command(linkwire::Next);uiTransport(1,now);}else command(linkwire::PlayPause);
  clickAt=clicks=0;
 }
}
void serialTick(){static String input;while(Serial.available()){char c=Serial.read();if(c=='\r')continue;if(c!='\n'){if(input.length()<100)input+=c;continue;}
 if(input=="status"){uiPrintStats();Serial.printf("[display] heap=%u min=%u largest=%u stack=%u wifi=%d ip=%s uart_bad=%u uart_drop=%u audio=%u gen=%u lines=%u pos=%u authoritative=%u\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),uxTaskGetStackHighWaterMark(nullptr),WiFi.status(),WiFi.localIP().toString().c_str(),wire.errors(),wire.txDropped,linkOnline,generation.load(),lyrics.count,playClock.position(millis()),playClock.authoritative);}
 else if(input.startsWith("brightness ")){uiRoundBrightness(constrain(input.substring(11).toInt(),0,255));uiPrintStats();}
 else if(input=="test")uiTest();else if(input=="setup"){prefs.putBool("setup",true);ESP.restart();}else if(input=="retry"){dirty=true;changedAt=millis();retryCount=0;}else if(input=="next")command(linkwire::Next);else if(input=="prev")command(linkwire::Previous);else if(input=="pause")command(linkwire::Pause);else if(input=="play")command(linkwire::Play);else if(input.startsWith("volume ")){desiredVolume=constrain(input.substring(7).toInt(),0,127);volumeIntentAt=millis();command(linkwire::Volume,desiredVolume);}else if(input.startsWith("offset ")){offsetMs=constrain(input.substring(7).toInt(),-10000,10000);prefs.putInt("offset",offsetMs);}else if(input.startsWith("bass ")){desiredBass=constrain(input.substring(5).toInt(),0,3);toneIntentAt=millis();command(linkwire::Bass,desiredBass);}else if(input=="lcd bgr"||input=="lcd flip"||input=="lcd invert"){uint8_t flags=prefs.getUChar("lcd",vinyl::DEFAULT_LCD_FLAGS)^(input=="lcd bgr"?1:input=="lcd flip"?2:4);prefs.putUChar("lcd",flags);uiLcdOptions(flags);Serial.printf("[lcd] flags=%u (bit0 BGR bit1 flip bit2 inversion)\n",flags);}else if(input=="invert"){inverted=!inverted;prefs.putBool("invert",inverted);}else Serial.println("Commands: status, brightness 0..255, test, setup, retry, next, prev, pause, volume 0..127, offset ms, bass 0..3, invert, lcd bgr|flip|invert");input="";
}}
void setup(){Serial.begin(115200);delay(300);Serial.println("\n[vinyl-dual] role=display firmware=0.4.11 C3 WiFi+2TFT+encoder no-SD");wire.begin(LINK_RX,LINK_TX);Serial.printf("[pins] MOSI=%d SCLK=%d DC=%d RST=%d round_CS=%d lyric_CS=%d encoder_CLK=%d DT=%d SW=%d UART_RX=%d TX=%d\n",DISPLAY_MOSI,DISPLAY_SCLK,DISPLAY_DC,DISPLAY_RST,ROUND_CS,LYRIC_CS,ENC_CLK,ENC_DT,ENC_SW,LINK_RX,LINK_TX);session=esp_random();if(!session)session=1;
 pinMode(ENC_CLK,INPUT_PULLUP);pinMode(ENC_DT,INPUT_PULLUP);pinMode(ENC_SW,INPUT_PULLUP);
 uiBegin();prefs.begin("vinyl-c3",false);uint8_t lcd=vinyl::loadLcdOptions(prefs);uiLcdOptions(lcd);Serial.printf("[lcd] profile=%u flags=%u inversion=%s\n",vinyl::LCD_PROFILE,lcd,lcd&4?"on":"off");offsetMs=prefs.getInt("offset",-120);oledMode=vinyl::OLEDMode(std::min(unsigned(prefs.getUChar("mode",0)),2u));visualMode=vinyl::VisualMode(std::min(unsigned(prefs.getUChar("visual",0)),4u));inverted=prefs.getBool("invert",false);
 esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
 SetupDisplay display;display.show(SetupStage::Boot);bool setupRequested=prefs.getBool("setup",false);prefs.putBool("setup",false);configureWiFi(prefs,display,setupRequested);
 lyrics.text.words=lyricWords;lyrics.cues.entries=cueWords;
 if(LittleFS.begin(true)){cacheFS=&LittleFS;if(!LittleFS.exists("/cache"))LittleFS.mkdir("/cache");Serial.printf("[cache] LittleFS ready used=%u total=%u art=96x96\n",unsigned(LittleFS.usedBytes()),unsigned(LittleFS.totalBytes()));}else Serial.println("[cache] LittleFS unavailable; artwork cache disabled");
 requests=xQueueCreate(1,sizeof(Track));results=xQueueCreate(1,sizeof(Result));consumed=xQueueCreate(1,1);
 if(!requests||!results||!consumed){Serial.println("[fatal] queue allocation");return;}
 if(xTaskCreatePinnedToCore(networkTask,"lookup",10240,nullptr,1,nullptr,0)!=pdPASS){Serial.println("[fatal] lookup task");return;}
 if(xTaskCreatePinnedToCore(encoderTask,"encoder",2048,nullptr,3,nullptr,0)!=pdPASS){Serial.println("[fatal] encoder task");return;}
 configTime(0,0,"pool.ntp.org","time.cloudflare.com");WiFi.setAutoReconnect(true);otaBeginServer();uiRestore();ready=true;Serial.printf("[display] Ready heap=%u min=%u ip=%s UART RX%d TX%d 921600\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),WiFi.localIP().toString().c_str(),LINK_RX,LINK_TX);
}
void loop(){if(!ready){delay(100);return;}transportTick();handleResults();serialTick();otaTick();if(otaBusy()){delay(2);return;}uiConnectivity(WiFi.status()==WL_CONNECTED);uint32_t now=millis();controls(now);
 static bool wasWiFi=false;bool wifi=WiFi.status()==WL_CONNECTED;if(wifi&&!wasWiFi&&track.title[0]&&!lyrics.count){dirty=true;changedAt=now;}wasWiFi=wifi;
 if(retryAt&&int32_t(now-retryAt)>=0&&wifi&&track.title[0]){++retryCount;retryAt=0;dirty=true;changedAt=now;}
 if(memoryWait.ready(generation,now,heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),wifi&&!runtimeBudget.background)){if(track.title[0]&&!lyrics.count){dirty=true;changedAt=now;}}
 if(dirty&&track.title[0]&&now-changedAt>=(track.artist[0]?350u:900u)){track.generation=generation;xQueueOverwrite(requests,&track);dirty=false;}
 static uint32_t healthAt=0;if(now-healthAt>=10000){healthAt=now;uiPrintStats();Serial.printf("[display] heap=%u min=%u largest=%u stack=%u wifi=%d uart_bad=%u uart_drop=%u lines=%u pos=%u authoritative=%u\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),uxTaskGetStackHighWaterMark(nullptr),WiFi.status(),wire.errors(),wire.txDropped,lyrics.count,playClock.position(now),playClock.authoritative);}
 uiTick(now);delay(2);
}
