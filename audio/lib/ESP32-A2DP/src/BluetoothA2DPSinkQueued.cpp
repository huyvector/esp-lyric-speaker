
#include "BluetoothA2DPSinkQueued.h"
#include <algorithm>

#if IS_VALID_PLATFORM

void BluetoothA2DPSinkQueued::bt_i2s_task_start_up(void) {
    if (s_bt_i2s_task_handle) return; // Duplicate connected event: reuse live output.
    ESP_LOGI(BT_APP_TAG, "ringbuffer data empty! mode changed: RINGBUFFER_MODE_PREFETCHING");
    ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    if ((s_i2s_write_semaphore = xSemaphoreCreateBinary()) == nullptr) {
        ESP_LOGE(BT_APP_TAG, "%s, Semaphore create failed", __func__);
        return;
    }
    if ((s_ringbuf_i2s = xRingbufferCreate(i2s_ringbuffer_size, RINGBUF_TYPE_BYTEBUF)) == nullptr) {
        ESP_LOGE(BT_APP_TAG, "%s, ringbuffer create failed", __func__);
        bt_i2s_task_shut_down();
        return;
    }
    //xTaskCreate(bt_i2s_task_handler, "BtI2STask", 2048, nullptr, configMAX_PRIORITIES - 3, &s_bt_i2s_task_handle);
    BaseType_t result = xTaskCreatePinnedToCore(ccall_i2s_task_handler, "BtI2STask", i2s_stack_size, nullptr, i2s_task_priority, &s_bt_i2s_task_handle, task_core);
    if (result!=pdPASS){
        ESP_LOGE(BT_AV_TAG, "xTaskCreatePinnedToCore");
        bt_i2s_task_shut_down();
    } else {
        ESP_LOGI(BT_AV_TAG, "BtI2STask Started");
    }
}

void BluetoothA2DPSinkQueued::bt_i2s_task_shut_down(void) {
    if(keep_output_alive && s_bt_i2s_task_handle) { input_active.store(false);user_paused.store(false);flush_request.fetch_add(1);return; }
    audio_buffered.store(0);
    if (s_bt_i2s_task_handle) {
        vTaskDelete(s_bt_i2s_task_handle);
        s_bt_i2s_task_handle = nullptr;
    }
    if (s_ringbuf_i2s) {
        vRingbufferDelete(s_ringbuf_i2s);
        s_ringbuf_i2s = nullptr;
    }
    if (s_i2s_write_semaphore) {
        vSemaphoreDelete(s_i2s_write_semaphore);
        s_i2s_write_semaphore = nullptr;
    }

    ESP_LOGI(BT_AV_TAG, "BtI2STask shutdown");
}

/* NEW I2S Task & ring buffer */

void BluetoothA2DPSinkQueued::i2s_task_handler(void *arg) {
    uint8_t *data = nullptr;
    size_t item_size = 0;
    /**
     * The total length of DMA buffer of I2S is:
     * `dma_frame_num * dma_desc_num * i2s_channel_num * i2s_data_bit_width / 8`.
     * Transmit `dma_frame_num * dma_desc_num` bytes to DMA is trade-off.
     */
    is_starting = true;
    uint32_t stackChecks = 0;

    while (true) {
        if(flush_request.load()!=flush_ack.load()){
            size_t n=0;uint8_t* discard;
            while((discard=(uint8_t*)xRingbufferReceiveUpTo(s_ringbuf_i2s,&n,0,i2s_write_size_upto)))vRingbufferReturnItem(s_ringbuf_i2s,discard);
            out->set_output_active(false);out->set_output_active(true);
            audio_buffered.store(0);ringbuffer_mode=RINGBUFFER_MODE_PREFETCHING;is_starting=true;
            force_drain.store(false);flush_ack.store(flush_request.load());
        }
        if(user_paused.load()){
            if(output_filter&&feedback_pending&&feedback_pending()){
                memset(mono_output,0,sizeof mono_output);output_filter(mono_output,sizeof mono_output);
                (void)i2s_write_data(mono_output,sizeof mono_output);
            } else vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if(force_drain.exchange(false)){
            ringbuffer_mode=RINGBUFFER_MODE_PROCESSING;is_starting=false;
        }
        if (is_starting){
            // wait for ringbuffer to be filled
            if (pdTRUE != xSemaphoreTake(s_i2s_write_semaphore, keep_output_alive ? pdMS_TO_TICKS(feedback_pending&&feedback_pending()?0:50) : portMAX_DELAY)){
                if(output_filter&&feedback_pending&&feedback_pending()){
                    memset(mono_output,0,sizeof mono_output);output_filter(mono_output,sizeof mono_output);
                    (void)i2s_write_data(mono_output,sizeof mono_output);
                }
                continue;
            }
            is_starting = false;
        }
        // xSemaphoreTake was succeeding here, so we have the buffer filled up
        item_size = 0;

        if ((stackChecks++ & 127) == 0) audio_stack_free.store(uxTaskGetStackHighWaterMark(nullptr), std::memory_order_relaxed);
        // receive data from ringbuffer and write it to I2S DMA transmit buffer 
        size_t limit = mono_queue ? std::min(i2s_write_size_upto, sizeof mono_output / 2) : i2s_write_size_upto;
        data = (uint8_t *)xRingbufferReceiveUpTo(s_ringbuf_i2s, &item_size, (TickType_t)pdMS_TO_TICKS(i2s_ticks), limit);
        if (item_size == 0) {
            ESP_LOGI(BT_APP_TAG, "ringbuffer underflowed! mode changed: RINGBUFFER_MODE_PREFETCHING");
            // Re-enter the semaphore wait after every underrun, not just boot.
            if (is_i2s_active && (!keep_output_alive || (input_active.load()&&!end_of_input.load()))) audio_counters.underruns.fetch_add(1, std::memory_order_relaxed);
            is_starting = true;
            ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
            // Cover a producer fill between the receive timeout and mode change.
            size_t available = 0;
            vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, nullptr, &available);
            audio_buffered.store(available, std::memory_order_relaxed);
            if (available >= size_t(i2s_ringbuffer_prefetch_size())) {
                ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
                (void)xSemaphoreGive(s_i2s_write_semaphore);
            }
            continue;
        } 

        // if i2s is not active we just consume the buffer w/o output
        if (is_i2s_active && is_output && (!keep_output_alive || input_active.load())){
            audio_counters.write(uint32_t(xTaskGetTickCount() * portTICK_PERIOD_MS));
            const uint8_t* output = data;
            size_t output_size = item_size;
            if (mono_queue) {
                output_size = a2dp_mono::expandStereo(data, item_size, mono_output, sizeof mono_output);
                output = mono_output;
            }
            if(output_filter&&output_size)output_filter(const_cast<uint8_t*>(output),output_size);
            size_t written = output_size ? i2s_write_data(output, output_size) : 0;
            if(frame_observer&&written)frame_observer(written/4);
            if (!output_size || written != output_size) {
                audio_counters.outputErrors.fetch_add(1, std::memory_order_relaxed);
                audio_counters.dropped.fetch_add((output_size ? output_size : item_size * 2) - std::min(written, output_size), std::memory_order_relaxed);
                // Health reporting happens from the low-priority app loop.
                // The ring item must be returned even if output failed.
            }
        }

        vRingbufferReturnItem(s_ringbuf_i2s, (void *)data);
        size_t available = 0;
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, nullptr, &available);
        audio_buffered.store(available, std::memory_order_relaxed);
        // i2s_write_data blocks on DMA; adding a fixed sleep here reduces
        // throughput and can make a small PCM ring overflow.
    }
}

size_t BluetoothA2DPSinkQueued::write_audio(const uint8_t *data, size_t size)
{
    size_t item_size = 0;
    BaseType_t done = pdFALSE;

    // PCM callbacks must never initialize hardware, sleep, log, or allocate.
    if (!data || !size || !s_ringbuf_i2s) return 0;
    if (!is_i2s_active || (keep_output_alive&&!input_active.load())) { audio_counters.dropped.fetch_add(size, std::memory_order_relaxed); return 0; }
    audio_counters.receive(size, uint32_t(xTaskGetTickCount() * portTICK_PERIOD_MS));

    if (ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, nullptr, &item_size);
#else
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, &item_size);
#endif
        if (item_size <= i2s_ringbuffer_prefetch_size()) {

            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        if (ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {
            audio_counters.dropped.fetch_add(size, std::memory_order_relaxed); return 0;
        }
    }

    const size_t input_size = size;
    if (mono_queue) {
        size = a2dp_mono::packLeftInPlace(const_cast<uint8_t*>(data), size);
        if (!size) { audio_counters.dropped.fetch_add(input_size, std::memory_order_relaxed); return 0; }
    }
    done = xRingbufferSend(s_ringbuf_i2s, (void *)data, size, (TickType_t)0);

    if (!done) {

        audio_counters.overflows.fetch_add(1, std::memory_order_relaxed);
        audio_counters.dropped.fetch_add(input_size, std::memory_order_relaxed);
        ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
    } else audio_counters.accepted.fetch_add(input_size, std::memory_order_relaxed);

    // Capture fill level in this live callback; snapshots never dereference a
    // ring handle that might have been freed during a disconnect.
    if (done || ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, nullptr, &item_size);
#else
        vRingbufferGetInfo(s_ringbuf_i2s, nullptr, nullptr, nullptr, &item_size);
#endif

        audio_buffered.store(item_size, std::memory_order_relaxed);
        if (ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING && item_size >= size_t(i2s_ringbuffer_prefetch_size())) {

            ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
            // A full binary semaphore already has a wake-up pending. This is
            // coalescing, not an audio failure; the consumer will take it.
            (void)xSemaphoreGive(s_i2s_write_semaphore);
        }
    }

    return done ? input_size : 0;
}

#endif // platform
