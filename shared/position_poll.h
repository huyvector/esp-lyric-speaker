#pragma once
#include <cstdint>
namespace vinyl {
struct PositionPoll {
  bool connected = false, pending = false;
  uint8_t label = 4, failures = 0;
  uint32_t sentAt = 0, dueAt = 0, generation = 0;
  void invalidate(uint32_t now) { pending = false; dueAt = now + 1100; }
  void connect(bool value, uint32_t now) { connected = value; pending = false; failures = 0; dueAt = now + 1100; }
  // Negative means idle. Exactly one request outstanding; other AVRCP labels
  // (0..4) belong to metadata/capabilities/notifications in the A2DP library.
  int request(uint32_t now, uint32_t gen) {
    if (!connected) return -1;
    if (pending) {
      if (uint32_t(now - sentAt) < 2500) return -1;
      pending = false; if (failures < 20) ++failures;
      dueAt = now + (failures >= 3 ? 15000 : 3000); return -2;
    }
    if (int32_t(now - dueAt) < 0) return -1;
    label = label == 15 ? 5 : label + 1;
    sentAt = now; generation = gen; pending = true; return label;
  }
  bool accept(uint8_t replyLabel, uint32_t now, uint32_t gen, bool valid) {
    if (!pending || replyLabel != label || gen != generation || uint32_t(now - sentAt) >= 2500) return false;
    pending = false;
    if (valid) failures = 0; else if (failures < 20) ++failures;
    dueAt = now + (failures >= 3 ? 15000 : 2000); return true;
  }
};
}
