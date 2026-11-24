"""Exercise actual queued sink methods with deterministic FreeRTOS/I2S doubles."""
from pathlib import Path
import subprocess, tempfile
base = Path(__file__).resolve().parents[1]
s = (base/'audio/lib/ESP32-A2DP/src/BluetoothA2DPSinkQueued.cpp').read_text()
audio = s[s.index('void BluetoothA2DPSinkQueued::i2s_task_handler'):s.rindex('#endif')]
stubs = r'''
#include <cassert>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <cstring>
#include "A2DPMonoQueue.h"
#include "BluetoothA2DPAudioStats.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) (++errors)
#define ESP_IDF_VERSION 40407
#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define pdMS_TO_TICKS(x) (x)
using TickType_t=unsigned;
using BaseType_t=int;
constexpr int pdTRUE=1,pdFALSE=0,portMAX_DELAY=~0u;
enum A2DPRingBufferMode {RINGBUFFER_MODE_PROCESSING,RINGBUFFER_MODE_PREFETCHING,RINGBUFFER_MODE_DROPPING};
int errors=0,takes=0,receives=0,returns=0,gives=0,delays=0,scenario=0;
size_t buffered=4096;bool sendOK=true;uint32_t tick=100;
constexpr unsigned portTICK_PERIOD_MS=1;
uint32_t xTaskGetTickCount(){return tick;}
unsigned uxTaskGetStackHighWaterMark(void*){return 1024;}
struct Stop {};
int xSemaphoreTake(void*,unsigned) { ++takes; if(takes==2) throw Stop{}; return pdTRUE; }
int xSemaphoreGive(void*) { ++gives; return pdFALSE; } // Wake already pending.
uint8_t sample[16];
std::vector<uint8_t> queued,outputBytes;bool outputSuccess=false;
uint8_t*receivePtr=sample;size_t receiveSize=sizeof sample;
void* xRingbufferReceiveUpTo(void*,size_t* size,unsigned,size_t) {
 ++receives;
 if(receives>1) throw Stop{};
 *size=scenario==1 ? 0 : receiveSize;
 return *size ? receivePtr : nullptr;
}
void vRingbufferReturnItem(void*,void*) { ++returns; }
int xRingbufferSend(void*,void*p,size_t n,unsigned) { if(sendOK)queued.assign((uint8_t*)p,(uint8_t*)p+n);return sendOK?pdTRUE:pdFALSE; }
void vRingbufferGetInfo(void*,void*,void*,void*,void*,size_t* size) { *size=buffered; }
void delay_ms(int) { ++delays; }
void vTaskDelay(int) { ++delays; throw Stop{}; }
struct Output { void begin() {} void set_output_active(bool) {} } output;
class BluetoothA2DPSinkQueued {
 public:
 bool is_starting=true,is_i2s_active=true,is_output=true,mono_queue=false;
 bool keep_output_alive=false;
 std::atomic<bool> input_active{false},user_paused{false},force_drain{false},end_of_input{false};
 std::atomic<unsigned> flush_request{0},flush_ack{0};
 void (*output_filter)(uint8_t*,size_t)=nullptr;bool (*feedback_pending)()=nullptr;void (*frame_observer)(unsigned)=nullptr;
 uint8_t mono_output[1024];
 std::atomic<A2DPRingBufferMode> ringbuffer_mode{RINGBUFFER_MODE_PROCESSING};
 A2DPAudioCounters audio_counters;
 std::atomic<uint32_t> audio_stack_free{0},audio_buffered{0};
 void* s_i2s_write_semaphore=nullptr;
 void* s_ringbuf_i2s=(void*)1;
 int i2s_ticks=20;
 size_t i2s_write_size_upto=1440;
 Output* out=&output;
 int i2s_ringbuffer_prefetch_size() { return 4096; }
 size_t i2s_write_data(const uint8_t*p,size_t n) { outputBytes.assign(p,p+n);return outputSuccess?n:0; }
 void i2s_task_handler(void*);
 size_t write_audio(const uint8_t*,size_t);
};
'''
checks = r'''
int main() {
 { // Underrun must return to the semaphore wait before consuming more data.
  scenario=1; BluetoothA2DPSinkQueued sink;
  try { sink.i2s_task_handler(nullptr); } catch(Stop&) {}
  assert(takes==2 && receives==1);
 }
 { // Failed DMA output must release the borrowed item, allowing future writes.
  scenario=2; takes=receives=returns=0; BluetoothA2DPSinkQueued sink;
  try { sink.i2s_task_handler(nullptr); } catch(Stop&) {}
  assert(returns==1 && delays==0&&sink.audio_counters.outputErrors==1&&sink.audio_counters.dropped==sizeof sample);
 }
 { // Already-pending binary wake-up must not produce an error every prefill.
  errors=0; gives=0; BluetoothA2DPSinkQueued sink;
  sink.ringbuffer_mode=RINGBUFFER_MODE_PREFETCHING;
  assert(sink.write_audio(sample,sizeof sample)==sizeof sample);
  assert(gives==1 && errors==0 && sink.ringbuffer_mode==RINGBUFFER_MODE_PROCESSING);
 }
 { // Never sleep/start hardware in a PCM callback when output is inactive.
  BluetoothA2DPSinkQueued sink;sink.is_i2s_active=false;delays=0;
  assert(sink.write_audio(sample,sizeof sample)==0&&delays==0&&sink.audio_counters.dropped==sizeof sample);
 }
 { // Count overflow and recover without throwing away the next usable packet.
  BluetoothA2DPSinkQueued sink;sendOK=false;
  assert(sink.write_audio(sample,sizeof sample)==0&&sink.audio_counters.overflows==1);
  sendOK=true;buffered=8192;
  assert(sink.write_audio(sample,sizeof sample)==0);
  buffered=1024;assert(sink.write_audio(sample,sizeof sample)==sizeof sample);
  assert(sink.audio_counters.dropped==sizeof sample*2&&sink.audio_counters.accepted==sizeof sample);
 }
 { // Mono queue is byte-exact with the existing downmixed stereo output.
  BluetoothA2DPSinkQueued sink;sink.mono_queue=true;
  const uint8_t original[]={0,128,0,128,255,255,255,255,0,0,0,0,255,127,255,127};
  memcpy(sample,original,sizeof sample);sendOK=true;buffered=1024;
  assert(sink.write_audio(sample,sizeof sample)==sizeof sample&&queued.size()==sizeof sample/2);
  assert(sink.audio_counters.accepted==sizeof sample&&sink.audio_counters.dropped==0);
  receivePtr=queued.data();receiveSize=queued.size();outputSuccess=true;
  takes=receives=returns=0;scenario=2;
  try { sink.i2s_task_handler(nullptr); } catch(Stop&) {}
  assert(returns==1&&outputBytes.size()==sizeof original&&!memcmp(outputBytes.data(),original,sizeof original));
  assert(sink.audio_counters.outputErrors==0&&sink.audio_counters.dropped==0);
  outputSuccess=false;takes=receives=returns=0;
  try { sink.i2s_task_handler(nullptr); } catch(Stop&) {}
  assert(returns==1&&sink.audio_counters.outputErrors==1&&sink.audio_counters.dropped==sizeof original);
  receivePtr=sample;receiveSize=sizeof sample;
 }
 { // Unsigned packet-gap arithmetic across tick wrap; pause resets the anchor.
  A2DPAudioCounters counters;counters.receive(100,UINT32_MAX-10);counters.receive(200,20);
  assert(counters.gapMaxMs==31&&counters.received==300);
  counters.write(100);counters.write(128);assert(counters.snapshot().writeGapMaxMs==28);
  counters.lastRxMs=0;counters.receive(100,20000);assert(counters.gapMaxMs==31);
 }
}
'''
with tempfile.TemporaryDirectory() as temp:
 src=Path(temp)/'audio_queue.cpp'; exe=Path(temp)/'audio_queue'
 src.write_text(stubs+audio+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'audio/lib/ESP32-A2DP/src'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Queued audio underrun wake-up / short-output release / pending wake coalescing / inactive nonblocking / overflow recovery / counters and tick wrap: PASS')
