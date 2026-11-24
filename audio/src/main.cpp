#include <Arduino.h>
#include <Preferences.h>
#include <BluetoothA2DPSinkQueued.h>
#include <esp_bt.h>
#include "lyrics_core.h"
#include "bass_tone.h"
#include "tone_store.h"
#include "device_identity.h"
#include "audio_feedback.h"
#include "position_poll.h"
#include "avrc_sync_wire.h"
#include "serial_link.h"
#include "image_update.h"
#include <atomic>
constexpr char AUDIO_VERSION[]="0.4.11";
constexpr int DAC_BCK=13,DAC_LRCK=14,DAC_DIN=27;
constexpr int AUDIO_LINK_RX=22,AUDIO_LINK_TX=23;
struct Track {uint32_t generation=0,duration=0;char title[192]{},artist[128]{},album[128]{};};
struct {std::atomic<bool> streaming{false};}runtimeBudget;
std::atomic<uint32_t> generation{1},volumeMaxUs{0};
class SpeakerVolume : public A2DPVolumeControl {
public:
  vinyl::SmoothMonoVolume smooth;
  std::atomic<uint32_t> changes{0};
  void set_volume(uint8_t value) override { unsigned before=smooth.level(); smooth.set(value); if(before!=smooth.level()) changes.fetch_add(1); }
  void update_audio_data(Frame* data, uint16_t frames) override {
    uint32_t started=micros();smooth.process(reinterpret_cast<uint8_t*>(data),size_t(frames)*4);
    uint32_t elapsed=micros()-started;if(elapsed>volumeMaxUs.load())volumeMaxUs.store(elapsed);
  }
} speakerVolume;

BluetoothA2DPSinkQueued speaker;
vinyl::ToneStore toneStore;vinyl::BassTone bassTone;vinyl::AudioFeedback feedback;
std::atomic<uint32_t> bassMaxUs{0};
void bassAudio(const uint8_t*p,uint32_t n){uint32_t at=micros();bassTone.process(const_cast<uint8_t*>(p),n);uint32_t elapsed=micros()-at;if(elapsed>bassMaxUs.load())bassMaxUs=elapsed;}
void bassRate(uint16_t hz){bassTone.setRate(hz);feedback.setRate(hz);}
void outputFeedback(uint8_t*p,size_t n){feedback.process(p,n);}bool feedbackPending(){return feedback.pending();}
Preferences prefs;vinyl::Clock playClock;vinyl::PositionPoll positionPoll;
Track track;bool connected=false,dirty=true;uint32_t changedAt=0,positionSamples=0,lastPositionLog=0;char lyricStatus[40]{};
HardwareSerial bridgePort(2);SerialLink wire(bridgePort);uint32_t session=0,peer=0;
ImageUpdate updater;uint32_t otaAt=0,rebootAt=0;bool updating=false,ready=false;linkwire::Duplicate commands;uint8_t commandStatus=0;bool otaDone=false;uint32_t lastChunkAt=0;uint16_t lastChunkLen=0,lastChunkCRC=0;
void clearTrack(){positionPoll.invalidate(millis());track.generation=++generation;dirty=true;changedAt=millis();}
enum class EventType : uint8_t { Title, Artist, Album, Duration, Position, Playing, Connection, TrackChanged, AvrcConnection, StatusReply };
struct Event { EventType type; uint32_t value; char text[192]; uint32_t extra, receivedAt; uint8_t label, state, code; bool valid; };
QueueHandle_t events;
std::atomic<bool> eventOverflow{false};
void post(EventType type, uint32_t value = 0, const char* text = nullptr) {
  Event e{}; e.type = type; e.value = value; e.receivedAt = millis();
  if (text) {
    size_t n = vinyl::utf8Prefix(text, sizeof e.text - 1);
    memcpy(e.text, text, n); e.text[n] = 0;
    vinyl::vietnamese(e.text);
  }
  if (xQueueSend(events, &e, 0) != pdTRUE) eventOverflow.store(true);
}
void metadata(uint8_t type, const uint8_t* value) {
  const char* s = reinterpret_cast<const char*>(value);
  switch (type) {
    case ESP_AVRC_MD_ATTR_TITLE: post(EventType::Title, 0, s); break;
    case ESP_AVRC_MD_ATTR_ARTIST: post(EventType::Artist, 0, s); break;
    case ESP_AVRC_MD_ATTR_ALBUM: post(EventType::Album, 0, s); break;
    case ESP_AVRC_MD_ATTR_PLAYING_TIME: post(EventType::Duration, strtoul(s, nullptr, 10)); break;
  }
}
void positionChanged(uint32_t pos) { post(EventType::Position, pos); }
void avrcConnection(bool state) { post(EventType::AvrcConnection, state); }
extern "C" void vinyl_avrc_initial_notification(uint8_t event, uint32_t value) {
  if (event == 5) post(EventType::Position, value);
  if (event == 1) post(EventType::Playing, value == ESP_AVRC_PLAYBACK_PLAYING);
}
extern "C" void vinyl_avrc_status_response(uint8_t label, uint8_t code, bool valid, uint32_t duration, uint32_t position, uint8_t state) {
  Event e{}; e.type = EventType::StatusReply; e.value = position; e.extra = duration;
  e.label = label; e.state = state; e.code = code; e.valid = valid; e.receivedAt = millis();
  if (xQueueSend(events, &e, 0) != pdTRUE) eventOverflow.store(true);
}
void applyPosition(uint32_t position, uint32_t receivedAt, const char* source) {
  if (position == UINT32_MAX || (playClock.duration && uint64_t(position) > uint64_t(playClock.duration) + 2000)) return;
  int64_t correction = int64_t(position) - playClock.position(receivedAt);
  bool first = !playClock.authoritative;
  playClock.sync(position, receivedAt); ++positionSamples;
  if (first || correction > 1000 || correction < -1000 || uint32_t(receivedAt - lastPositionLog) >= 15000) {
    lastPositionLog = receivedAt;
    Serial.printf("[sync] source=%s position=%u correction=%d samples=%u authoritative=1\n", source, unsigned(position), int(correction), unsigned(positionSamples));
  }
}
void pollPosition(uint32_t now) {
  int label = positionPoll.request(now, generation.load());
  if (label == -2) { Serial.printf("[sync] GetPlayStatus timeout failures=%u; clock remains %s\n", positionPoll.failures, playClock.authoritative ? "interpolated from Bluetooth" : "estimated"); return; }
  if (label < 0) return;
  int error = vinyl_avrc_get_play_status(label);
  if (error) { positionPoll.accept(label, now, generation.load(), false); Serial.printf("[sync] GetPlayStatus send error=%d\n", error); }
}
void playbackChanged(esp_avrc_playback_stat_t state) { post(EventType::Playing, state == ESP_AVRC_PLAYBACK_PLAYING); }
void audioChanged(esp_a2d_audio_state_t state, void*) {
  if (state != ESP_A2D_AUDIO_STATE_STARTED) bassTone.reset();
  runtimeBudget.streaming.store(state == ESP_A2D_AUDIO_STATE_STARTED);
  post(EventType::Playing, state == ESP_A2D_AUDIO_STATE_STARTED);
}
void connectionChanged(esp_a2d_connection_state_t state, void*) { post(EventType::Connection, state == ESP_A2D_CONNECTION_STATE_CONNECTED); }
void trackChanged(uint8_t*) { post(EventType::TrackChanged); }

void handleEvents() {
  Event e;
  while (xQueueReceive(events, &e, 0) == pdTRUE) {
    uint32_t now = millis();
    switch (e.type) {
      case EventType::TrackChanged:
        track = Track{}; playClock.reset(now); clearTrack(); break;
      case EventType::Title:
      case EventType::Artist:
      case EventType::Album: {
        char* dest = e.type == EventType::Title ? track.title : e.type == EventType::Artist ? track.artist : track.album;
        size_t capacity = e.type == EventType::Title ? sizeof track.title : sizeof track.artist;
        if (strcmp(dest, e.text)) {
          // Some sources omit a track-change event. A changed title is also
          // sufficient to invalidate an in-flight result and reset the clock.
          if (e.type == EventType::Title && track.title[0]) playClock.reset(now);
          clearTrack(); size_t n = vinyl::utf8Prefix(e.text, capacity - 1); memcpy(dest, e.text, n); dest[n] = 0;
          vinyl::vietnamese(dest);
        }
        break;
      }
      case EventType::Duration:
        playClock.duration = e.value;
        if (e.value != track.duration) { track.duration = e.value; generation.fetch_add(1); dirty = true; changedAt = now; }
        break;
      case EventType::Position: applyPosition(e.value, e.receivedAt, "notification"); break;
      case EventType::AvrcConnection:
        positionPoll.connect(e.value, now);
        Serial.printf("[sync] AVRCP=%u notification_position=%u notification_playback=%u poll=GetPlayStatus/2s\n", e.value,
          e.value && speaker.is_avrc_peer_rn_cap(ESP_AVRC_RN_PLAY_POS_CHANGED),
          e.value && speaker.is_avrc_peer_rn_cap(ESP_AVRC_RN_PLAY_STATUS_CHANGE));
        break;
      case EventType::StatusReply: {
        bool known = e.valid && e.value != UINT32_MAX && e.state != ESP_AVRC_PLAYBACK_ERROR;
        if (!positionPoll.accept(e.label, e.receivedAt, generation.load(), known)) break;
        if (!known) { Serial.printf("[sync] GetPlayStatus unavailable code=0x%02x position=%u\n", e.code, unsigned(e.value)); break; }
        if (e.extra && e.extra != UINT32_MAX) playClock.duration = e.extra;
        playClock.setPlaying(e.state == ESP_AVRC_PLAYBACK_PLAYING, e.receivedAt);
        applyPosition(e.value, e.receivedAt, "GetPlayStatus");
        break;
      }
      case EventType::Playing: playClock.setPlaying(e.value, now); break;
      case EventType::Connection:
        connected = e.value;
        if (!connected) {
          runtimeBudget.streaming.store(false);
          positionPoll.connect(false, now);
          clearTrack(); dirty = false; track = Track{}; playClock.reset(now); playClock.setPlaying(false, now);
          strlcpy(lyricStatus, "PAIR IPHONE", sizeof lyricStatus);
        }
        break;
    }
  }
  if (eventOverflow.exchange(false)) {
    // Never keep displaying another song after a lost metadata event.
    clearTrack(); dirty = false;
    strlcpy(lyricStatus, "RECONNECT BLUETOOTH", sizeof lyricStatus);
    Serial.println("metadata queue overflow; reconnect source");
  }
}

void ack(const linkwire::Frame&f,uint8_t status,uint32_t offset=0){uint8_t p[6]={f.type,status};linkwire::put32(p+2,offset);wire.send(linkwire::Ack,p,sizeof p,f.seq);}
void sendHello(){uint8_t p[5];linkwire::put32(p,session);p[4]=0;wire.send(linkwire::Hello,p,sizeof p);}
bool sendTrack(){uint8_t p[460]{};linkwire::put32(p,session);linkwire::put32(p+4,generation.load());linkwire::put32(p+8,track.duration);memcpy(p+12,track.title,192);memcpy(p+204,track.artist,128);memcpy(p+332,track.album,128);return wire.send(linkwire::Track,p,sizeof p);}
void sendState(){linkwire::StateData s;s.session=session;s.generation=generation;s.position=playClock.position(millis());s.duration=playClock.duration;s.flags=(connected?linkwire::CONNECTED:0)|(playClock.playing?linkwire::PLAYING:0)|(playClock.authoritative?linkwire::AUTHORITATIVE:0)|(updating?linkwire::UPDATING:0);s.volume=speakerVolume.smooth.level();s.bass=bassTone.level();s.peak=feedback.takePeak();s.heap=ESP.getFreeHeap();uint8_t p[linkwire::STATE_BYTES];linkwire::packState(p,s);wire.send(linkwire::State,p,sizeof p);}
void receive(){unsigned budget=8;while(budget--&&wire.receive()){
 const auto&f=wire.frame;
 if(f.type==linkwire::Hello&&f.len==5){uint32_t id=linkwire::get32(f.data);if(peer!=id){peer=id;commands.valid=false;dirty=true;}continue;}
 if(f.type==linkwire::Command&&f.len==9&&!updating){uint32_t id=linkwire::get32(f.data);if(id!=peer||!id){ack(f,1);continue;}if(commands.seen(id,f)){ack(f,commandStatus);continue;}
  int value=int32_t(linkwire::get32(f.data+5));uint8_t result=0;
  switch(f.data[4]){case linkwire::Snapshot:sendHello();sendTrack();break;case linkwire::PlayPause:if(playClock.playing)speaker.pause();else speaker.play();break;case linkwire::Play:speaker.play();break;case linkwire::Pause:speaker.pause();break;case linkwire::Next:speaker.next();break;case linkwire::Previous:speaker.previous();break;case linkwire::Volume:speaker.set_volume(constrain(value,0,127));break;case linkwire::Bass:bassTone.setLevel(constrain(value,0,3));toneStore.changed(millis());break;case linkwire::Cue:feedback.beep();break;default:result=2;}
  commandStatus=result;ack(f,result);continue;
 }
 if(f.type==linkwire::OtaBegin&&f.len==37){if(f.data[0]!=0){ack(f,2);continue;}char md5[33];memcpy(md5,f.data+5,32);md5[32]=0;
  if(!ImageUpdate::valid(linkwire::get32(f.data+1),md5)){ack(f,3);continue;}
  if(!updating){speaker.set_volume(0);speaker.pause();speaker.end(false);updating=true;playClock.setPlaying(false,millis());Serial.println("[ota] audio stopped; receiving UART image");}
  otaDone=false;lastChunkLen=0;bool ok=updater.begin(linkwire::get32(f.data+1),md5,0);otaAt=millis();ack(f,ok?0:3);continue;
 }
 if(f.type==linkwire::OtaData&&updating&&f.len>4){uint32_t at=linkwire::get32(f.data);size_t n=f.len-4;otaAt=millis();
  if(at==lastChunkAt&&n==lastChunkLen&&at+n==updater.offset()&&linkwire::crc(f.data+4,n)==lastChunkCRC){ack(f,0,updater.offset());continue;}
  bool ok=!otaDone&&at==updater.offset()&&updater.write(f.data+4,n);if(ok){lastChunkAt=at;lastChunkLen=n;lastChunkCRC=linkwire::crc(f.data+4,n);}ack(f,ok?0:4,updater.offset());continue;
 }
 if(f.type==linkwire::OtaEnd&&updating){bool ok=otaDone||updater.finish();otaDone=ok;ack(f,ok?0:5);if(!rebootAt)rebootAt=millis()+3500;continue;}
 if(f.type==linkwire::OtaAbort&&updating){updater.abort();ack(f,0);rebootAt=millis()+500;}
}}
void health(){auto s=speaker.audio_stats();auto eq=bassTone.takeStats();uint32_t raw=speakerVolume.smooth.takePeak();Serial.printf("[audio] heap=%u min=%u rx=%u dropped=%u underrun=%u overflow=%u output_error=%u fill=%u/%u stack=%u eq_max_us=%u vol_max_us=%u uart_bad=%u uart_drop=%u\n",ESP.getFreeHeap(),ESP.getMinFreeHeap(),s.received,s.dropped,s.underruns,s.overflows,s.outputErrors,s.buffered,s.capacity,s.stackFree,bassMaxUs.load(),volumeMaxUs.load(),wire.errors(),wire.txDropped);Serial.printf("[dsp] rate=%u volume=%u bass=%u mono_raw_peak=%u eq_in_peak=%u eq_out_peak=%u eq_limited=%u rx_gap_max_ms=%u write_gap_max_ms=%u tone=%u\n",speaker.sample_rate(),speakerVolume.smooth.level(),bassTone.level(),raw,eq.inputPeak,eq.outputPeak,eq.limitedSamples,s.gapMaxMs,s.writeGapMaxMs,feedback.testToneActive());Serial.printf("[output] dac=PCM5100A format=I2S bits=16 slots=2 mono=LR-average clock_request=APLL rate=%u bck_target=%u mclk_pin=off BCK=%d LRCK=%d DIN=%d\n",speaker.sample_rate(),unsigned(speaker.sample_rate())*32,DAC_BCK,DAC_LRCK,DAC_DIN);}
void setup(){Serial.begin(115200);session=esp_random();if(!session)session=1;wire.begin(AUDIO_LINK_RX,AUDIO_LINK_TX);prefs.begin("vinyl",false);bassTone.setLevel(prefs.getUInt("bass",2));if(prefs.isKey("treble"))prefs.remove("treble");toneStore.begin(bassTone.level());
 events=xQueueCreate(16,sizeof(Event));if(!events){Serial.println("[fatal] event queue allocation failed");return;}
 Serial.printf("\n[vinyl-dual] role=audio firmware=%s dac=PCM5100A session=%u UART RX%d TX%d 921600 no-WiFi no-SD\n",AUDIO_VERSION,session,AUDIO_LINK_RX,AUDIO_LINK_TX);
 // WeAct PCM5100A: Philips I2S, 16-bit slots, BCK = 32 * LRCK. Its internal
 // BCK PLL supports this at both negotiated A2DP rates (44.1/48 kHz).
 // SmoothMonoVolume already mixes (L+R)/2; the output queue duplicates
 // that sample into both slots so either analogue output carries mono.
 i2s_pin_config_t pins{};pins.mck_io_num=I2S_PIN_NO_CHANGE;pins.bck_io_num=DAC_BCK;pins.ws_io_num=DAC_LRCK;pins.data_out_num=DAC_DIN;pins.data_in_num=I2S_PIN_NO_CHANGE;
 i2s_config_t cfg{};cfg.mode=i2s_mode_t(I2S_MODE_MASTER|I2S_MODE_TX);cfg.sample_rate=44100;cfg.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;cfg.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT;cfg.communication_format=I2S_COMM_FORMAT_STAND_I2S;cfg.intr_alloc_flags=ESP_INTR_FLAG_LEVEL1;cfg.dma_buf_count=8;cfg.dma_buf_len=128;cfg.tx_desc_auto_clear=true;
 // APLL follows sample-rate changes; fixed_mclk=0 avoids locking 48 kHz
 // sources to the 44.1 kHz startup clock. No external MCLK GPIO is used.
 cfg.bits_per_chan=I2S_BITS_PER_CHAN_16BIT;cfg.use_apll=true;cfg.fixed_mclk=0;cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;
 speaker.set_volume_control(&speakerVolume);speaker.set_pin_config(pins);speaker.set_i2s_config(cfg);speaker.set_mono_queue(true);speaker.set_output_feedback(outputFeedback,feedbackPending);speaker.set_stream_reader(bassAudio,true);speaker.set_sample_rate_callback(bassRate);
 speaker.set_i2s_ringbuffer_size(30720);speaker.set_i2s_ringbuffer_prefetch_percent(40);speaker.set_i2s_write_size_upto(1024);speaker.set_max_write_delay_ms(0);speaker.set_i2s_stack_size(2048);speaker.set_i2s_task_priority(24);speaker.set_task_priority(15);speaker.set_task_core(1);
 speaker.set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE|ESP_AVRC_MD_ATTR_ARTIST|ESP_AVRC_MD_ATTR_ALBUM|ESP_AVRC_MD_ATTR_PLAYING_TIME);speaker.set_avrc_connection_state_callback(avrcConnection);speaker.set_avrc_metadata_callback(metadata);speaker.set_avrc_rn_play_pos_callback(positionChanged,1);speaker.set_avrc_rn_playstatus_callback(playbackChanged);speaker.set_avrc_rn_track_change_callback(trackChanged);speaker.set_on_audio_state_changed(audioChanged);speaker.set_on_connection_state_changed(connectionChanged);speaker.set_volume(55);
 esp_bt_controller_mem_release(ESP_BT_MODE_BLE);esp_bt_controller_config_t bt=BT_CONTROLLER_INIT_CONFIG_DEFAULT();bt.mode=ESP_BT_MODE_CLASSIC_BT;bt.bt_max_acl_conn=1;
 if(esp_bt_controller_init(&bt)!=ESP_OK||esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)!=ESP_OK){Serial.println("[fatal] Bluetooth controller init");return;}
 speaker.set_auto_reconnect(true);speaker.start(vinyl::BLUETOOTH_NAME);playClock.reset(millis());sendHello();health();ready=true;
}
void loop(){if(!ready){delay(100);return;}uint32_t now=millis();receive();toneStore.tick(millis(),updating,bassTone.level(),prefs);if(events&&!updating){handleEvents();pollPosition(now);}
 static uint32_t stateAt=0,trackAt=0,healthAt=0;
 if(now-stateAt>=100){stateAt=now;sendState();}
 if(now-trackAt>=2000||(dirty&&now-changedAt>=350)){trackAt=now;sendHello();if(sendTrack())dirty=false;}
 if(now-healthAt>=10000){healthAt=now;health();}
 if(updating&&((rebootAt&&int32_t(now-rebootAt)>=0)||(!rebootAt&&now-otaAt>15000))){Serial.println("[ota] reboot audio");Serial.flush();ESP.restart();}
 static String input;while(Serial.available()){char c=Serial.read();if(c=='\r')continue;if(c!='\n'){if(input.length()<64)input+=c;continue;}
  if(input=="status")health();
  else if(!updating&&input=="tone 0"){feedback.stopTestTone();Serial.println("[test] tone stopped");}
  else if(!updating&&(input=="tone 200"||input=="tone 1000")){
   unsigned frequency=input.substring(5).toInt();speaker.pause();feedback.testTone(frequency);
   Serial.printf("[test] sine=%uHz duration=3s peak=4096 (-18dBFS); music pause requested; type play to resume\n",frequency);
  }
  else if(!updating&&input=="play"){feedback.stopTestTone();speaker.play();}
  else if(!updating&&input=="pause"){feedback.stopTestTone();speaker.pause();}
  else if(!updating&&input=="next"){feedback.stopTestTone();speaker.next();}
  else if(!updating&&input=="prev"){feedback.stopTestTone();speaker.previous();}
  else if(!updating&&input.startsWith("bass ")){unsigned value=constrain(input.substring(5).toInt(),0,3);bassTone.setLevel(value);toneStore.changed(millis());Serial.printf("[dsp] bass=%u\n",value);}
  else if(!updating&&input.startsWith("volume ")){speaker.set_volume(constrain(input.substring(7).toInt(),0,127));Serial.printf("[dsp] volume=%u\n",speakerVolume.smooth.level());}
  else Serial.println("Commands: status, play, pause, next, prev, bass 0..3, volume 0..127, tone 200, tone 1000, tone 0");
  input="";
 }
 delay(2);
}
