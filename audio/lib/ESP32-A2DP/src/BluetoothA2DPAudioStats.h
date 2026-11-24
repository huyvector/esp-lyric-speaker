#pragma once
#include <atomic>
#include <cstdint>
struct A2DPAudioSnapshot {
  uint32_t received, accepted, dropped, underruns, overflows, outputErrors, gapMaxMs, writeGapMaxMs, buffered, capacity, stackFree;
};
struct A2DPAudioCounters {
  std::atomic<uint32_t> received{0}, accepted{0}, dropped{0}, underruns{0}, overflows{0}, outputErrors{0};
  std::atomic<uint32_t> lastRxMs{0}, gapMaxMs{0}, lastWriteMs{0}, writeGapMaxMs{0};
  void receive(uint32_t bytes, uint32_t now) {
    received.fetch_add(bytes, std::memory_order_relaxed);
    uint32_t last = lastRxMs.exchange(now, std::memory_order_relaxed);
    if (!last) return;
    uint32_t gap = now - last, previous = gapMaxMs.load(std::memory_order_relaxed);
    while (gap > previous && !gapMaxMs.compare_exchange_weak(previous, gap, std::memory_order_relaxed)) {}
  }
  void write(uint32_t now) {
    uint32_t last = lastWriteMs.exchange(now, std::memory_order_relaxed);
    if (!last) return;
    uint32_t gap = now - last, previous = writeGapMaxMs.load(std::memory_order_relaxed);
    while (gap > previous && !writeGapMaxMs.compare_exchange_weak(previous, gap, std::memory_order_relaxed)) {}
  }
  A2DPAudioSnapshot snapshot() const {
    return {received.load(), accepted.load(), dropped.load(), underruns.load(), overflows.load(), outputErrors.load(), gapMaxMs.load(), writeGapMaxMs.load(), 0, 0, 0};
  }
};

static_assert(ATOMIC_INT_LOCK_FREE == 2, "PCM counters must use lock-free 32-bit atomics");
