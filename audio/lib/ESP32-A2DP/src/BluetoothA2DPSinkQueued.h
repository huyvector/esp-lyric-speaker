#pragma once

#include "BluetoothA2DPSink.h"
#include "BluetoothA2DPAudioStats.h"
#include "A2DPMonoQueue.h"

#if IS_VALID_PLATFORM

#define RINGBUF_HIGHEST_WATER_LEVEL (32 * 1024)
#define RINGBUF_PREFETCH_PERCENT 65

enum A2DPRingBufferMode : char {
  RINGBUFFER_MODE_PROCESSING,  /* ringbuffer is buffering incoming audio data,
                                  I2S is working */
  RINGBUFFER_MODE_PREFETCHING, /* ringbuffer is buffering incoming audio data,
                                  I2S is waiting */
  RINGBUFFER_MODE_DROPPING /* ringbuffer is not buffering (dropping) incoming
                              audio data, I2S is working */
};

/**
 * @brief The BluetoothA2DPSinkQueued is using a separate Task with an additinal
 * Queue to write the I2S data. application.
 * @ingroup a2dp
 * @author Phil Schatzmann
 * @copyright Apache License Version 2
 */
class BluetoothA2DPSinkQueued : public BluetoothA2DPSink {
 public:
  BluetoothA2DPSinkQueued() = default;
  ~BluetoothA2DPSinkQueued(){keep_output_alive=false;bt_i2s_task_shut_down();}
  void end(bool release=false) override {keep_output_alive=false;bt_i2s_task_shut_down();BluetoothA2DPSink::end(release);}

#if A2DP_I2S_AUDIOTOOLS
  /// Output AudioOutput using AudioTools library
  BluetoothA2DPSinkQueued(audio_tools::AudioOutput &output) {
    actual_bluetooth_a2dp_sink = this;
    out->set_output(output);
  }
  /// Output AudioStream using AudioTools library
  BluetoothA2DPSinkQueued(audio_tools::AudioStream &output) {
    actual_bluetooth_a2dp_sink = this;
    out->set_output(output);
  }
#endif

#ifdef ARDUINO
  /// Output to Arduino Print
  BluetoothA2DPSinkQueued(Print &output) {
    actual_bluetooth_a2dp_sink = this;
    out->set_output(output);
  }
#endif

  // Configure before start(): store one already-downmixed channel in the
  // ring, expand to the unchanged stereo I2S slots only at the consumer.
  void set_mono_queue(bool enabled) { mono_queue = enabled; if (enabled) set_mono_downmix(true); }
  bool mono_queue_enabled() const { return mono_queue; }
  unsigned prefill_bytes() { return i2s_ringbuffer_prefetch_size(); }

  /// Defines the stack size of the i2s task (in bytes)
  void set_i2s_stack_size(int size) { i2s_stack_size = size; }

  /// Defines the ringbuffer size used by the i2s task (in bytes)
  void set_i2s_ringbuffer_size(int size) { i2s_ringbuffer_size = size; }

  /// Audio starts to play when limit exeeded
  void set_i2s_ringbuffer_prefetch_percent(int percent) {
    if (percent < 0) return;
    if (percent > 100) return;
    ringbuffer_prefetch_percent = percent;
  }

  /// Defines the priority of the I2S task
  void set_i2s_task_priority(UBaseType_t prio) { i2s_task_priority = prio; }

  void set_i2s_write_size_upto(size_t size) { i2s_write_size_upto = size; }

  void set_i2s_ticks(int ticks) { i2s_ticks = ticks; }

  A2DPAudioSnapshot audio_stats() {
    auto result = audio_counters.snapshot();
    result.buffered = audio_buffered.load(); result.capacity = i2s_ringbuffer_size;
    // No task/ring handle is followed here: disconnect may delete them.
    result.stackFree = audio_stack_free.load();
    return result;
  }

  // Opt-in persistent I2S owner for notification PCM, also used by local SD.
  void set_output_feedback(void (*filter)(uint8_t*,size_t),bool (*pending)()) { output_filter=filter; feedback_pending=pending; keep_output_alive=true; }
  void local_begin(){init_i2s();set_i2s_active(true);}
  void local_rate(unsigned rate){m_sample_rate=rate;out->set_sample_rate(rate);}
  size_t local_write(const uint8_t* data,size_t bytes){return write_audio(data,bytes);}
  void local_pause(bool pause){user_paused.store(pause);}
  void local_drain(){end_of_input.store(true);force_drain.store(true);}
  void local_flush(){end_of_input.store(false);flush_request.fetch_add(1);}
  bool local_flushed()const{return flush_request.load()==flush_ack.load();}
  void set_frame_observer(void (*fn)(unsigned)){frame_observer=fn;}
 protected:
  bool keep_output_alive=false;
  void (*output_filter)(uint8_t*,size_t)=nullptr;
  bool (*feedback_pending)()=nullptr;
  void (*frame_observer)(unsigned)=nullptr;
  std::atomic<bool> input_active{false},user_paused{false},force_drain{false},end_of_input{false};
  std::atomic<unsigned> flush_request{0},flush_ack{0};
  void init_i2s() override { BluetoothA2DPSink::init_i2s();if(keep_output_alive)bt_i2s_task_start_up(); }
  bool mono_queue = false;
  alignas(4) uint8_t mono_output[1024]; // One fixed output chunk, no callback allocation.
  A2DPAudioCounters audio_counters;
  std::atomic<uint32_t> audio_stack_free{0}, audio_buffered{0};
  TaskHandle_t s_bt_i2s_task_handle = nullptr; /* handle of I2S task */
  RingbufHandle_t s_ringbuf_i2s = nullptr;    /* handle of ringbuffer for I2S */
  SemaphoreHandle_t s_i2s_write_semaphore = nullptr;
  // I2S task
  int i2s_stack_size = 2048;
  int i2s_ringbuffer_size = RINGBUF_HIGHEST_WATER_LEVEL;
  UBaseType_t i2s_task_priority = configMAX_PRIORITIES - 3;
  std::atomic<A2DPRingBufferMode> ringbuffer_mode{RINGBUFFER_MODE_PROCESSING};
  std::atomic<bool> is_starting{true};
  size_t i2s_write_size_upto = 240 * 6;
  int i2s_ticks = 20;
  int ringbuffer_prefetch_percent = RINGBUF_PREFETCH_PERCENT;

  void bt_i2s_task_start_up(void) override;
  void bt_i2s_task_shut_down(void) override;
  void i2s_task_handler(void *arg) override;
  size_t write_audio(const uint8_t *data, size_t size) override;

  void set_i2s_active(bool active) override {
    audio_counters.lastRxMs.store(0); audio_counters.lastWriteMs.store(0);
    input_active.store(active);
    if(keep_output_alive){
      // Keep I2S clock running for quiet idle notifications. No second writer.
      if(!active)out->set_output_active(false);
      out->set_output_active(true);is_i2s_active=true;
    } else BluetoothA2DPSink::set_i2s_active(active);
    if (active) {
      end_of_input.store(false);
      ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
      is_starting = true;
    }
  }

  int i2s_ringbuffer_prefetch_size() {
    int bytes = i2s_ringbuffer_size * ringbuffer_prefetch_percent / 100;
    return (bytes / 4 * 4);
  }
};

#endif // platform
