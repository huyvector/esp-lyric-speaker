#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
// AVRCP vendor-dependent PDUs; bounds checked before reading phone data.
typedef struct { uint32_t duration, position; uint8_t state; } vinyl_play_status_t;
static inline uint32_t vinyl_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline bool vinyl_parse_play_status(const uint8_t* p, size_t n, vinyl_play_status_t* out) {
  if (!p || !out || n != 13 || p[0] != 0x30 || p[1] != 0 || p[2] != 0 || p[3] != 9) return false;
  out->duration = vinyl_be32(p + 4); out->position = vinyl_be32(p + 8); out->state = p[12];
  return out->state <= 4 || out->state == 0xff;
}
static inline bool vinyl_parse_initial_notification(const uint8_t* p, size_t n, uint8_t* event, uint32_t* value) {
  if (!p || !event || !value || n < 6 || p[0] != 0x31 || p[1] != 0 || p[2] != 0 || n != (size_t)p[3] + 4) return false;
  *event = p[4];
  if (*event == 1 && n == 6) { *value = p[5]; return true; }
  if (*event == 5 && n == 9) { *value = vinyl_be32(p + 5); return true; }
  return false;
}
#ifdef __cplusplus
extern "C" {
#endif
// Implemented by the build-local IDF 4.4 AVRCP bridge. Queue into BTC context.
int vinyl_avrc_get_play_status(uint8_t label);
// Implemented by main: enqueue only, no UI/network/storage in BTC context.
void vinyl_avrc_status_response(uint8_t label, uint8_t code, bool valid, uint32_t duration, uint32_t position, uint8_t state);
void vinyl_avrc_initial_notification(uint8_t event, uint32_t value);
#ifdef __cplusplus
}
#endif
