"""Actual I2S loop: idle/paused cue, short EOF drain and flush ownership."""
from pathlib import Path
import ast,subprocess,tempfile
base=Path(__file__).resolve().parents[1];s=(base/'audio/lib/ESP32-A2DP/src/BluetoothA2DPSinkQueued.cpp').read_text();code=s[s.index('void BluetoothA2DPSinkQueued::i2s_task_handler'):s.rindex('#endif')]
tree=ast.parse((base/'test/audio_queue_test.py').read_text());stub=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='stubs' for t in n.targets))
a=stub.index('int xSemaphoreTake(');z=stub.index('int xSemaphoreGive(',a)
stub=stub[:a]+'''int xSemaphoreTake(void*,unsigned) { ++takes; if(takes==2) throw Stop{}; return scenario==3?pdFALSE:pdTRUE; }
'''+stub[z:]
stub=stub.replace('if(receives>1) throw Stop{};','if(scenario==5){*size=receives==1?receiveSize:0;return *size?receivePtr:nullptr;}\n if(receives>1) throw Stop{};')
stub=stub.replace('std::vector<uint8_t> queued,outputBytes;','int writes=0,filters=0,frames=0;\nstd::vector<uint8_t> queued,outputBytes;').replace('void set_output_active(bool) {}','void set_output_active(bool) {}').replace('outputBytes.assign(p,p+n);','++writes;outputBytes.assign(p,p+n);if(scenario==4)throw Stop{};')
checks=r'''
bool cue=true;bool pending(){return cue;}void filter(uint8_t*p,size_t n){++filters;for(size_t i=0;i<n;++i)assert(!p[i]);p[0]=7;cue=false;}void observe(unsigned n){frames+=n;}
int main(){
 auto reset=[](){takes=receives=returns=filters=writes=frames=0;outputSuccess=true;cue=true;};
 {reset();scenario=3;BluetoothA2DPSinkQueued sink;sink.keep_output_alive=true;sink.output_filter=filter;sink.feedback_pending=pending;
 try{sink.i2s_task_handler(nullptr);}catch(Stop&){}assert(writes==1&&filters==1&&receives==0&&outputBytes[0]==7&&sink.audio_counters.underruns==0);}
 {reset();scenario=4;BluetoothA2DPSinkQueued sink;sink.keep_output_alive=true;sink.user_paused=true;sink.output_filter=filter;sink.feedback_pending=pending;
 try{sink.i2s_task_handler(nullptr);}catch(Stop&){}assert(writes==1&&filters==1&&receives==0);}
 {reset();scenario=2;BluetoothA2DPSinkQueued sink;sink.keep_output_alive=true;sink.input_active=true;sink.force_drain=true;sink.frame_observer=observe;
 try{sink.i2s_task_handler(nullptr);}catch(Stop&){}assert(!takes&&writes==1&&returns==1&&frames==4);}
 {reset();scenario=5;BluetoothA2DPSinkQueued sink;sink.keep_output_alive=true;sink.flush_request=1;sink.user_paused=true;cue=false;
 try{sink.i2s_task_handler(nullptr);}catch(Stop&){}assert(sink.flush_ack==1&&returns==1&&receives==2&&!writes&&!sink.audio_buffered);}
 {reset();scenario=2;BluetoothA2DPSinkQueued sink;sink.keep_output_alive=true;assert(sink.write_audio(sample,sizeof sample)==0&&sink.audio_counters.dropped==sizeof sample);}
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'feedback.cpp').write_text(stub+code+checks);subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'audio/lib/ESP32-A2DP/src'),str(p/'feedback.cpp'),'-o',str(p/'feedback')],check=True);subprocess.run([str(p/'feedback')],check=True)
print('Actual output loop: idle cue without ring, paused cue retains music, short EOF drain bypasses prefill, acknowledged flush, inactive input rejected; same sole I2S writer: PASS')
