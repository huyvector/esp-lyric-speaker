"""Actual queued-output resource ownership under failed allocations/reconnects."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];s=(base/'audio/lib/ESP32-A2DP/src/BluetoothA2DPSinkQueued.cpp').read_text()
methods=s[s.index('void BluetoothA2DPSinkQueued::bt_i2s_task_start_up'):s.index('/* NEW I2S Task')]
stub=r'''
#include <atomic>
#include <cassert>
#include <iostream>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define RINGBUF_TYPE_BYTEBUF 0
#define pdPASS 1
#define RINGBUFFER_MODE_PREFETCHING 0
using BaseType_t=int;
int fail=0,live=0,creates=0;
void* alloc(int stage){++creates;if(fail==stage)return nullptr;++live;return new int(stage);}
void release(void*p){assert(p);--live;delete static_cast<int*>(p);}
void* xSemaphoreCreateBinary(){return alloc(1);}
void* xRingbufferCreate(int,int){return alloc(2);}
void ccall_i2s_task_handler(void*){}
int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,void**task,int){*task=alloc(3);return *task?pdPASS:0;}
void vTaskDelete(void*p){release(p);}void vRingbufferDelete(void*p){release(p);}void vSemaphoreDelete(void*p){release(p);}
class BluetoothA2DPSinkQueued{
public:
 void*s_bt_i2s_task_handle=nullptr;void*s_ringbuf_i2s=nullptr;void*s_i2s_write_semaphore=nullptr;
 int ringbuffer_mode=0,i2s_ringbuffer_size=16384,i2s_stack_size=2048,i2s_task_priority=24,task_core=1;
 bool keep_output_alive=false;std::atomic<bool>input_active{false},user_paused{false};std::atomic<unsigned>flush_request{0};
 std::atomic<unsigned>audio_buffered{0};
 void bt_i2s_task_start_up();void bt_i2s_task_shut_down();
};
'''
checks=r'''
int main(){
 for(fail=1;fail<=3;++fail){BluetoothA2DPSinkQueued sink;sink.bt_i2s_task_start_up();assert(live==0);
  assert(!sink.s_ringbuf_i2s&&!sink.s_i2s_write_semaphore&&!sink.s_bt_i2s_task_handle);
  sink.bt_i2s_task_shut_down();assert(live==0);
 }
 fail=0;BluetoothA2DPSinkQueued sink;
 for(unsigned i=0;i<20;++i){sink.bt_i2s_task_start_up();assert(live==3);int at=creates;
  sink.bt_i2s_task_start_up();assert(live==3&&creates==at);
  sink.bt_i2s_task_shut_down();sink.bt_i2s_task_shut_down();assert(live==0);
 }
 sink.keep_output_alive=true;sink.bt_i2s_task_start_up();assert(live==3);sink.input_active=true;sink.user_paused=true;
 sink.bt_i2s_task_shut_down();assert(live==3&&!sink.input_active&&!sink.user_paused&&sink.flush_request==1);
 int at=creates;sink.bt_i2s_task_start_up();assert(creates==at);sink.keep_output_alive=false;sink.bt_i2s_task_shut_down();assert(live==0);
 std::cout<<"Actual audio lifecycle: semaphore/ring/task allocation failure cleanup, duplicate connect, 20 reconnects, double shutdown: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'life.cpp').write_text(stub+methods+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined',str(p/'life.cpp'),'-o',str(p/'life')],check=True)
 subprocess.run([str(p/'life')],check=True)
