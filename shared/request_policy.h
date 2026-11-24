#pragma once
#include <cstdint>
#include <cstddef>
namespace vinyl {
enum class Service : uint8_t { LyricsGet, LyricsSearch, Catalog, Cover, Count };
inline const char* serviceName(Service service) {
  switch (service) {
    case Service::LyricsGet: return "lyrics-get";
    case Service::LyricsSearch: return "lyrics-search";
    case Service::Catalog: return "catalog";
    default: return "cover";
  }
}
struct LyricRoute {
  bool searchFirst;
  uint32_t wait;
};
inline LyricRoute lyricRoute(bool collaborative, uint32_t getWait, uint32_t searchWait) {
  if (getWait && searchWait) return {collaborative, getWait < searchWait ? getWait : searchWait};
  return {getWait ? true : searchWait ? false : collaborative, 0};
}
// Endpoint 503 failures are independent; 429 is shared across a host.
// All waits stay below INT32_MAX so subtraction remains safe across millis wrap.
class RequestGate {
  struct Deadline {
    uint32_t until = 0;
    bool active = false;
    uint32_t remaining(uint32_t now) const {
      int32_t delta = int32_t(until - now);
      return active && delta > 0 ? uint32_t(delta) : 0;
    }
  };
  Deadline endpoint[size_t(Service::Count)], host[3];
  uint8_t failures[size_t(Service::Count)]{};
  static size_t hostIndex(Service service) {
    return service == Service::LyricsGet || service == Service::LyricsSearch ? 0 : service == Service::Catalog ? 1 : 2;
  }
public:
  uint32_t remaining(Service service, uint32_t now) const {
    uint32_t a = endpoint[size_t(service)].remaining(now), b = host[hostIndex(service)].remaining(now);
    return a > b ? a : b;
  }
  void succeeded(Service service) { failures[size_t(service)] = 0; endpoint[size_t(service)] = Deadline{}; }
  void failed(Service service, int status, uint32_t retrySeconds, uint32_t now) {
    if (status != 429 && status != 503) return;
    uint32_t seconds = !retrySeconds ? 30 : retrySeconds > 86400 ? 86400 : retrySeconds;
    if (status == 503) {
      unsigned n = failures[size_t(service)];
      if (n < 4) ++failures[size_t(service)];
      uint32_t backoff = 2u << (n < 4 ? n : 4);
      if (backoff > 30) backoff = 30;
      if (seconds < backoff) seconds = backoff;
    }
    Deadline& deadline = status == 429 ? host[hostIndex(service)] : endpoint[size_t(service)];
    uint32_t delay = seconds * 1000u;
    if (deadline.remaining(now) > delay) return; // Do not shorten Retry-After.
    deadline.until = now + delay; deadline.active = true;
  }
};
inline bool advertisement(const char* artist) {
  const char* word = "advertisement";
  while (*artist == ' ' || *artist == '\t') ++artist;
  for (; *word; ++word, ++artist) {
    unsigned char c = *artist;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != *word) return false;
  }
  unsigned char c = *artist;
  return !c || c == ' ' || c == '\t' || c >= 128;
}
}
