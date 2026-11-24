Upstream v1.8.8, Apache-2.0. Local changes in v0.2.2:

* Queued I2S: re-arm prefetch/semaphore wait after underrun; pending binary wake-ups coalesce without an error; return ring item even on failed output; remove extra fixed 5 ms delay after DMA write.
* read_address: absent first-run connected_bda namespace is expected, log other NVS open errors with their error name.
* Build manifest excludes unused BluetoothA2DPSource.cpp; A2DP_SPP_SUPPORT=0 selects receiver without SPP.

2026-11-03 / v0.3.0: optional persistent queued I2S owner for local SD PCM and idle/paused feedback. Feedback/peak filter executes in the consumer only; no second DMA writer. Atomic pause retains music in ring; flush discards ring and DMA with ACK before changing local song/rate. EOF drain bypasses short-song prefill and does not count intentional end as underrun. Default resource lifecycle retained unless opted in; explicit end/destructor force cleanup. Local source selected at reboot, never decoding MP3 alongside an active BT controller.
