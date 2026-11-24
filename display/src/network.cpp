#include "app.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include <memory>
#include <new>
#include "trust_anchors.h"
#include "request_policy.h"
#include "metadata_policy.h"
#include "storage_manager.h"

namespace {
constexpr size_t MAX_BODY = 384 * 1024;
constexpr uint32_t CACHE_MAGIC = 0x31564C43;
vinyl::RequestGate requestGate;
uint32_t requestRetryMs = 30000;
int lastHTTP = 0;
const char* requestFailure = "LYRICS NET ERROR";
uint32_t lookupStartedAt = 0;
struct Header { uint32_t magic, length, crc; };
bool current(const Track& t) { return t.generation == generation.load(); }
void publish(const Track& t, ResultKind kind, const char* status, uint32_t retryAfterMs = 0, uint16_t length = 0, uint16_t offset = 0) {
  if (!current(t)) return;
  Result r{t.generation, kind, "", retryAfterMs, length, offset}; strlcpy(r.status, status, sizeof r.status);
  xQueueSend(results, &r, portMAX_DELAY);
  // Main always acknowledges even a stale result, so staging never changes
  // while the UI is copying it. This wait yields the CPU to audio/UI tasks.
  uint8_t ack; xQueueReceive(consumed, &ack, portMAX_DELAY);
  if (kind == ResultKind::Lyrics || kind == ResultKind::LyricsEnd)
    Serial.printf("[lookup] gen=%u lyrics_ready=%ums from=worker_start status=%s\n",t.generation,unsigned(millis()-lookupStartedAt),status);
}
String encoded(const char* value) {
  String s; s.reserve(strlen(value) * 3);
  const char* hex = "0123456789ABCDEF";
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(value); *p; ++p) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') s += char(*p);
    else { s += '%'; s += hex[*p >> 4]; s += hex[*p & 15]; }
  }
  return s;
}
String normalized(String s) {
  vinyl::vietnamese(s.begin(), true);
  vinyl::foldComparison(s.begin());
  s = String(s.c_str());
  s.toLowerCase(); s.trim();
  // Keep UTF-8 bytes intact; do not fold away Vietnamese letters.
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = s[i];
    if (c < 128 && !isalnum(c)) s.setCharAt(i, ' ');
  }
  while (s.indexOf("  ") >= 0) s.replace("  ", " ");
  s.trim(); return s;
}
String cleaned(String s) {
  const char* decorations[] = {"(Official Music Video)", "(Official Video)", "(Official Audio)", "[Official MV]", "(Lyrics)", "[Lyrics]", "(Lyric Video)"};
  for (const char* d : decorations) {
    String lower = s; lower.toLowerCase(); String needle(d); needle.toLowerCase();
    int at = lower.indexOf(needle); if (at >= 0) s.remove(at, strlen(d));
  }
  vinyl::cleanGuestCredits(s.begin());
  s = String(s.c_str()); s.trim(); return s;
}
String primaryArtist(const char* artist) {
  String value(artist); value.remove(vinyl::primaryArtistLength(artist)); value.trim();
  return value;
}
float similarity(String a, String b) {
  a = normalized(a); b = normalized(b);
  if (!a.length() || !b.length()) return 0;
  if (a == b) return 1;
  unsigned matched = 0, total = 0;
  for (size_t i = 0; i < a.length();) {
    int end = a.indexOf(' ', i); if (end < 0) end = a.length();
    String token = a.substring(i, end); ++total;
    if ((" " + b + " ").indexOf(" " + token + " ") >= 0) ++matched;
    i = end + 1;
  }
  return float(matched) / std::max(1u, total);
}
float matchScore(const char* title, const char* artist, float duration, const Track& t) {
  float ts = similarity(t.title, title);
  float as = std::max(similarity(t.artist, artist), similarity(primaryArtist(t.artist), artist));
  if (ts < .65f || (t.artist[0] && as < .34f)) return -1;
  float difference = t.duration && duration > 0 ? fabsf(t.duration / 1000.f - duration) : 0;
  if (difference > 15) return -1; // no forced retiming between different releases
  String wanted = normalized(t.title), candidate = normalized(title);
  const char* versions[] = {"remix", "live", "cover", "acoustic", "slowed", "sped"};
  for (const char* v : versions) if (candidate.indexOf(v) >= 0 && wanted.indexOf(v) < 0) return -1;
  return .67f * ts + .23f * (t.artist[0] ? as : .5f) + .10f * (1 - difference / 20);
}

class LimitedStream : public Stream {
  Client& source; const Track& track;
  size_t remaining = MAX_BODY;
  uint32_t deadline;
  uint8_t buffer[512]; size_t position = 0, buffered = 0;
  bool fill() {
    if (!alive()) return false;
    if (position < buffered) return true;
    int available = source.available();
    if (available <= 0) return false;
    size_t amount = std::min(sizeof buffer, std::min(remaining, size_t(available)));
    int got = source.read(buffer, amount); // One TLS call for a block, not per JSON byte.
    if (got <= 0) return false;
    position = 0; buffered = size_t(got);
    vTaskDelay(1); // Give audio/UI a scheduling point for each received block.
    return true;
  }
 public:
  LimitedStream(Client& s, const Track& t) : source(s), track(t), deadline(millis() + 18000) { setTimeout(2500); }
  bool alive() const { return remaining && current(track) && int32_t(deadline - millis()) > 0; }
  int available() override { if (!alive()) return 0; return position < buffered ? int(buffered-position) : source.available(); }
  int read() override { if (!fill()) return -1; --remaining; return buffer[position++]; }
  int peek() override { return fill() ? buffer[position] : -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
};
int nextSeparator(Stream& stream) {
  char c;
  do { if (stream.readBytes(&c, 1) != 1) return -1; } while (c == ' ' || c == '\r' || c == '\n' || c == '\t');
  return uint8_t(c);
}
String reqHost(const String& url) {
  String host = url.substring(8); int slash = host.indexOf('/'); if (slash >= 0) host.remove(slash);
  return host;
}
vinyl::Service requestService(const String& url) {
  if (url.startsWith("https://lrclib.net/api/search")) return vinyl::Service::LyricsSearch;
  if (url.startsWith("https://lrclib.net/api/get")) return vinyl::Service::LyricsGet;
  if (url.startsWith("https://itunes.apple.com/")) return vinyl::Service::Catalog;
  return vinyl::Service::Cover;
}
uint32_t retryDelay(uint32_t wait) {
  uint32_t jitter = std::min<uint32_t>(750u, wait/4);
  return wait + 1000 + (jitter ? esp_random() % (jitter+1) : 0);
}
uint32_t lastTLSFinished = 0;
struct Request {
  bool opened = false;
  WiFiClientSecure client; HTTPClient http;
  bool open(const String& url, const Track& t) {
    lastHTTP = 0; requestFailure = "LYRICS NET ERROR"; requestRetryMs = 30000;
    vinyl::Service service = requestService(url);
    if (!current(t) || WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) return false;
    uint32_t wait = requestGate.remaining(service, millis());
    if (wait) {
      requestFailure = "SERVICE BUSY"; requestRetryMs = retryDelay(wait);
      Serial.printf("[https] gen=%u service=%s deferred=%ums\n", t.generation, vinyl::serviceName(service), unsigned(wait));
      return false;
    }
    uint32_t admittedAt = millis();
    while (current(t) && (!runtimeBudget.allowNetwork(millis()) ||
           (lastTLSFinished && uint32_t(millis() - lastTLSFinished) < 300))) {
      if (uint32_t(millis() - admittedAt) >= 1500) {
        requestFailure = "AUDIO BUSY"; requestRetryMs = 3000;
        Serial.printf("[https] gen=%u deferred for audio fill=%u\n", t.generation, runtimeBudget.buffered.load());
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!current(t)) return false;
    // A2DP + UI continue if HTTPS cannot be afforded on this board.
    size_t dram = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (dram < 42000 || largest < 18000) {
      requestFailure = "LOW TLS MEMORY";
      Serial.printf("[https] gen=%u deferred memory dram=%u largest=%u required=42000/18000\n", t.generation, unsigned(dram), unsigned(largest));
      return false;
    }
    // Native trust-anchor validation stops at the locally trusted GTS R4.
    // The Arduino 2.x bundle callback instead follows the server's cross-sign
    // to a removed GlobalSign root and fails with X509_FATAL_ERROR (-12288).
    client.setCACert(vinyl::SERVICE_ROOTS); client.setHandshakeTimeout(6);
    http.useHTTP10(true); // Stream JSON directly, no chunk framing or getString()
    http.setConnectTimeout(4000); http.setTimeout(2500);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, url)) return false;
    const char* watched[] = {"Retry-After"}; http.collectHeaders(watched, 1);
    http.addHeader("User-Agent", "ESP32VinylDual/0.4.2 personal-device");
    uint32_t started = millis();
    opened = true;
    int status = http.GET(); lastHTTP = status;
    if (!current(t)) { Serial.printf("[https] gen=%u cancelled after track change\n", t.generation); return false; }
    if (status != 200 || http.getSize() > int(MAX_BODY)) {
      if (status == 429 || status == 503) {
        long parsed = http.header("Retry-After").toInt();
        requestGate.failed(service, status, parsed > 0 ? uint32_t(parsed) : (status == 503 ? 3u : 30u), millis());
        requestRetryMs = retryDelay(requestGate.remaining(service, millis()));
      }
      char tlsMessage[160]{};
      int tlsCode = status < 0 ? client.lastError(tlsMessage, sizeof tlsMessage) : 0;
      // Arduino stores the successful socket descriptor here too (e.g. +54).
      // It is not a negative mbedTLS error and must not go through strerror.
      if (tlsCode >= 0) { tlsCode = 0; tlsMessage[0] = 0; }
      String transport = status < 0 ? HTTPClient::errorToString(status) : String();
      requestFailure = status == 404 ? "NO MATCH" : (status == 429 || status == 503 ? "SERVICE BUSY" :
        (tlsCode == -12288 || tlsCode == -9984 ? "TLS CERT ERROR" : "LYRICS NET ERROR"));
      // Do not dump URL queries or credentials. Record transport/TLS details
      // before the client is destroyed; HTTP -1 alone hides the actual cause.
      String host = reqHost(url);
      Serial.printf("[https] gen=%u service=%s host=%s http=%d transport=%s tls=%d message=%s elapsed=%ums dram8=%u largest8=%u stack=%u epoch=%lld rssi=%d\n",
        t.generation, vinyl::serviceName(service), host.c_str(), status, transport.c_str(), tlsCode, tlsMessage, unsigned(millis() - started),
        heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        uxTaskGetStackHighWaterMark(nullptr), static_cast<long long>(time(nullptr)), WiFi.RSSI());
      return false;
    }
    requestGate.succeeded(service);
    Serial.printf("[https] gen=%u service=%s host=%s http=200 elapsed=%ums stack=%u\n", t.generation, vinyl::serviceName(service), reqHost(url).c_str(), unsigned(millis() - started), uxTaskGetStackHighWaterMark(nullptr));
    return true;
  }
  ~Request() { http.end(); client.stop(); if (opened) lastTLSFinished = millis(); }
};

// Cache data is raw UTF-8 .lrc or 96x96 RGB565 .rgb. A small .crc
// sidecar verifies length/CRC before any part is shown. Old prefixed files
// remain readable, preserving existing user cache across app-only updates.
constexpr size_t CACHE_BLOCK = 512;
uint32_t crcStep(uint32_t state, const uint8_t* data, size_t n) {
  while (n--) { state ^= *data++; for (int i = 0; i < 8; ++i) state = (state >> 1) ^ (0xEDB88320u & (0u - (state & 1))); }
  return state;
}
bool streamCache(const Track& t, const String& path, bool art) {
  if (!cacheFS) return false;
  File f = cacheFS->open(path, FILE_READ); if (!f) return false;
  Header h{}; size_t start = 0;
  File meta = cacheFS->open(path + ".crc", FILE_READ);
  if (meta) {
    if (meta.size() != sizeof h || meta.read(reinterpret_cast<uint8_t*>(&h), sizeof h) != sizeof h) return false;
    meta.close();
  } else {
    if (f.read(reinterpret_cast<uint8_t*>(&h), sizeof h) != sizeof h) return false;
    start = sizeof h;
  }
  if (h.magic != CACHE_MAGIC || h.length == 0 || h.length > vinyl::TRANSFER_LIMIT ||
      (art ? h.length != vinyl::ART_BYTES : h.length >= vinyl::TEXT_LIMIT) || f.size() != start + h.length) return false;
  if (!f.seek(start)) return false;
  uint32_t crc = ~0u;
  for (size_t at = 0; at < h.length;) {
    if (!current(t)) return false;
    size_t amount = std::min(CACHE_BLOCK, size_t(h.length) - at);
    if (f.read(staging, amount) != amount) return false;
    crc = crcStep(crc, staging, amount); at += amount; vTaskDelay(1);
  }
  if (~crc != h.crc || !f.seek(start)) { Serial.printf("[cache] rejected corrupt file=%s\n", path.c_str()); return false; }
  publish(t, art ? ResultKind::ArtworkStart : ResultKind::LyricsStart, "READING CACHE");
  size_t lineSize = 0;
  uint8_t block[CACHE_BLOCK];
  bool ok = true;
  for (size_t at = 0; ok && at < h.length;) {
    if (!current(t)) return false;
    size_t amount = std::min(CACHE_BLOCK, size_t(h.length) - at);
    // Lyrics need a separate I/O chunk while staging holds the current line.
    uint8_t* target = art ? staging : block;
    if (f.read(target, amount) != amount) { ok = false; break; }
    if (art) publish(t, ResultKind::ArtworkChunk, "", 0, amount, at);
    else for (size_t i = 0; i < amount; ++i) {
      if (block[i] == '\n') {
        staging[lineSize] = 0;
        publish(t, ResultKind::LyricsLine, "", 0, lineSize); lineSize = 0;
      } else if (block[i] && lineSize < vinyl::TEXT_LIMIT - 1) staging[lineSize++] = block[i];
      else { ok = false; break; }
    }
    at += amount; vTaskDelay(1);
  }
  if (ok && !art && lineSize) { staging[lineSize] = 0; publish(t, ResultKind::LyricsLine, "", 0, lineSize); }
  publish(t, ok ? (art ? ResultKind::ArtworkEnd : ResultKind::LyricsEnd) : (art ? ResultKind::ArtworkAbort : ResultKind::LyricsAbort), art ? "CACHED ART" : "CACHED");
  Serial.printf("[cache] %s gen=%u file=%s bytes=%u block=512\n", ok ? "hit" : "read-failed", t.generation, path.c_str(), unsigned(h.length));
  return ok && current(t);
}
// Reserve space for incoming data and keep room for temporary JPEG/CRC files.
constexpr size_t CACHE_BYTES=512*1024;
void pruneCache(size_t reserve=0) {
 if(!cacheFS)return;
 for(unsigned pass=0;pass<96;++pass){
  size_t bytes=0;unsigned assets=0;String first;
  File dir=cacheFS->open("/cache");
  for(File f=dir.openNextFile();f;f=dir.openNextFile()){
   String path=f.path();
   if(path.endsWith(".lrc")||path.endsWith(".rgb")){bytes+=f.size()+sizeof(Header);++assets;if(first.isEmpty())first=path;}
  }
  dir.close();
  if((bytes+reserve<=CACHE_BYTES&&assets<48)||first.isEmpty())return;
  if(!cacheFS->remove(first))return;
  cacheFS->remove(first+".crc");vTaskDelay(1);
 }
}
void writeCache(const String& path, size_t length) {
  if (!cacheFS) return;
  pruneCache(length+sizeof(Header));
  String tmp = path + ".tmp", meta = path + ".crc", metaTmp = meta + ".tmp";
  File f = cacheFS->open(tmp, FILE_WRITE); if (!f) return;
  uint32_t checksum = ~0u; bool ok = true;
  for (size_t at = 0; ok && at < length;) {
    size_t amount = std::min(CACHE_BLOCK, length - at);
    ok = f.write(staging + at, amount) == amount;
    checksum = crcStep(checksum, staging + at, amount); at += amount; vTaskDelay(1);
  }
  f.close(); Header h{CACHE_MAGIC, uint32_t(length), ~checksum};
  File m;
  if (ok) { m = cacheFS->open(metaTmp, FILE_WRITE); ok = m && m.write(reinterpret_cast<uint8_t*>(&h), sizeof h) == sizeof h; m.close(); }
  // The metadata commit is last. Interrupted or incomplete pairs fail CRC
  // validation and are fetched again; unrelated flash cache entries are retained.
  if (ok) {
    cacheFS->remove(path); ok = cacheFS->rename(tmp, path);
    if (ok) { cacheFS->remove(meta); ok = cacheFS->rename(metaTmp, meta); }
  }
  if (!ok) { cacheFS->remove(tmp); cacheFS->remove(metaTmp); Serial.printf("[cache] write failed file=%s (full flash or I/O)\n", path.c_str()); }
  else Serial.printf("[cache] saved file=%s bytes=%u retention=%s\n", path.c_str(), unsigned(length), "bounded-flash");
}
struct StagingAllocator {
  void* allocate(size_t size) { return size <= vinyl::JSON_LIMIT ? staging : nullptr; }
  void deallocate(void*) {}
  void* reallocate(void*, size_t size) { return allocate(size); }
};
constexpr int SEARCH_INLINE = -2;
int searchLyrics(const Track& t, const String& path) {
  Request req;
  String url = "https://lrclib.net/api/search?track_name=" + encoded(t.title);
  if (!req.open(url, t)) return -1;
  LimitedStream stream(*req.http.getStreamPtr(), t);
  if (!stream.find("[")) return -1;
  StaticJsonDocument<256> filter;
  for (const char* key : {"id", "trackName", "artistName", "duration", "syncedLyrics"}) filter[key] = true;
  BasicJsonDocument<StagingAllocator> doc(vinyl::JSON_LIMIT); int best = -1; float score = .66f;
  for (int i = 0; i < 20 && stream.alive(); ++i) {
    auto error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    if (error) {
      if (!current(t)) Serial.printf("[lyrics] gen=%u cancelled during search\n", t.generation);
      else { requestFailure = error == DeserializationError::NoMemory ? "LYRICS TOO LARGE" : "LRC FORMAT ERROR"; Serial.printf("[lyrics] gen=%u search JSON=%s\n", t.generation, error.c_str()); }
      return best;
    }
    float s = matchScore(doc["trackName"] | "", doc["artistName"] | "", doc["duration"] | 0.f, t);
    if (s > score) { score = s; best = doc["id"] | -1; }
    const char* lrc = doc["syncedLyrics"] | "";
    size_t length = strlen(lrc);
    if (s >= .82f && length && length < vinyl::TEXT_LIMIT && current(t)) {
      // Search already contains LRC. Do not require a second /get request.
      // Close TLS before moving the arena or writing cache; no duplicate LRC.
      req.http.end(); req.client.stop();
      memmove(staging, lrc, length + 1); vinyl::vietnamese(reinterpret_cast<char*>(staging));
      length = strlen(reinterpret_cast<char*>(staging));
      publish(t, ResultKind::Lyrics, "SYNCED SEARCH"); writeCache(path, length);
      Serial.printf("[lyrics] gen=%u source=search bytes=%u score=%.2f\n", t.generation, unsigned(length), double(s));
      return SEARCH_INLINE;
    }
    int c = nextSeparator(stream);
    if (c != ',') break;
    vTaskDelay(1);
  }
  if (best < 0) requestFailure = "NO MATCH";
  return best;
}
bool fetchLyrics(const Track& t, int id, const String& path) {
  // Reuse the transfer arena for filtered JSON: never hold two LRC copies.
  lastHTTP = 0; requestFailure = "LRC FORMAT ERROR";
  BasicJsonDocument<StagingAllocator> doc(vinyl::JSON_LIMIT);
  StaticJsonDocument<128> filter;
  filter["syncedLyrics"] = true; filter["instrumental"] = true;
  {
    Request req;
    String url;
    if (id >= 0) url = "https://lrclib.net/api/get/" + String(id);
    else {
      if (!t.artist[0]) return false;
      url = "https://lrclib.net/api/get?track_name=" + encoded(t.title) + "&artist_name=" + encoded(t.artist);
      if (t.duration >= 1000 && t.duration <= 3600000) url += "&duration=" + String(t.duration / 1000.f, 2);
    }
    if (!req.open(url, t)) return false;
    LimitedStream stream(*req.http.getStreamPtr(), t);
    auto error = deserializeJson(doc, stream, DeserializationOption::Filter(filter));
    if (error && !current(t)) { Serial.printf("[lyrics] gen=%u cancelled during parse\n", t.generation); return false; }
    if (error) { requestFailure = "LRC FORMAT ERROR"; Serial.printf("LRC JSON: %s\n", error.c_str()); return false; }
  } // Free TLS before parsing, cache writes, and artwork decoding.
  if (doc["instrumental"] | false) { publish(t, ResultKind::Status, "INSTRUMENTAL"); return true; }
  const char* lrc = doc["syncedLyrics"] | "";
  if (!*lrc) { publish(t, ResultKind::Status, "NO TIMED LYRICS"); return true; }
  size_t length = strlen(lrc);
  if (length >= vinyl::TEXT_LIMIT) { publish(t, ResultKind::Status, "LYRICS TOO LARGE"); return true; }
  if (!current(t)) return false;
  memmove(staging, lrc, length + 1); vinyl::vietnamese(reinterpret_cast<char*>(staging));
  length = strlen(reinterpret_cast<char*>(staging));
  publish(t, ResultKind::Lyrics, "SYNCED"); // Display first; flash commit follows the ownership ACK.
  writeCache(path, length); return true;
}

String findArtwork(const Track& t, bool& albumMatches) {
  albumMatches = false;
  Request req;
  String url;
  { // Release term and encoded temporaries before TLS starts.
    String term = String(t.title) + " " + t.artist;
    url = "https://itunes.apple.com/search?entity=song&media=music&country=VN&limit=5&term=" + encoded(term.c_str());
  }
  if (!req.open(url, t)) return "";
  LimitedStream stream(*req.http.getStreamPtr(), t);
  if (!stream.find("[")) return "";
  StaticJsonDocument<256> filter;
  for (const char* k : {"trackName", "artistName", "trackTimeMillis", "artworkUrl100", "collectionName"}) filter[k] = true;
  DynamicJsonDocument doc(2048); String best; float score = .72f;
  for (int i = 0; i < 5 && stream.alive(); ++i) {
    if (deserializeJson(doc, stream, DeserializationOption::Filter(filter))) break;
    float s = matchScore(doc["trackName"] | "", doc["artistName"] | "", (doc["trackTimeMillis"] | 0.f) / 1000, t);
    if (s > score) {
      score = s; best = doc["artworkUrl100"] | "";
      albumMatches = t.album[0] && normalized(t.album) == normalized(doc["collectionName"] | "") &&
        (normalized(t.artist) == normalized(doc["artistName"] | "") || normalized(primaryArtist(t.artist)) == normalized(doc["artistName"] | ""));
    }
    int c = nextSeparator(stream);
    if (c != ',') break;
  }
  // Bound source dimensions; JPEGs from the catalog are normally baseline.
  // Retain artworkUrl100 from the API; do not request the old 64px variant.
  return best;
}
File jpegFile;
int sourceWidth = 0, sourceHeight = 0;
int drawJPEG(JPEGDRAW* block) {
  // Resample small baseline JPEG to exactly 96x96 without a second framebuffer.
  uint16_t* pixels = reinterpret_cast<uint16_t*>(staging);
  for (int dy = 0; dy < vinyl::ART_SIZE; ++dy) {
    int sy = dy * sourceHeight / vinyl::ART_SIZE;
    if (sy < block->y || sy >= block->y + block->iHeight) continue;
    for (int dx = 0; dx < vinyl::ART_SIZE; ++dx) {
      int sx = dx * sourceWidth / vinyl::ART_SIZE;
      if (sx >= block->x && sx < block->x + block->iWidth)
        pixels[dy * vinyl::ART_SIZE + dx] = block->pPixels[(sy - block->y) * block->iWidth + sx - block->x];
    }
  }
  return 1;
}
int32_t jpegRead(JPEGFILE* handle, uint8_t* buffer, int32_t length) { return static_cast<File*>(handle->fHandle)->read(buffer, length); }
int32_t jpegSeek(JPEGFILE* handle, int32_t offset) { return static_cast<File*>(handle->fHandle)->seek(offset) ? offset : -1; }
void jpegClose(void* handle) { static_cast<File*>(handle)->close(); }
bool fetchArtwork(const Track& t, const String& url, const String& path, const String& albumPath) {
  if (!cacheFS || url.isEmpty() || !current(t)) return false;
  const char* temp = "/cache/download.jpg";
  {
    Request req; if (!req.open(url, t)) return false;
    int size = req.http.getSize();
    if (size > 24576) return false;
    File f = cacheFS->open(temp, FILE_WRITE); if (!f) return false;
    Stream* stream = req.http.getStreamPtr(); size_t total = 0; uint32_t last = millis();
    uint8_t chunk[512];
    while (current(t) && total < 24576 && (size < 0 || total < size) && millis() - last < 3000) {
      size_t available = stream->available();
      if (!available) { if (!req.client.connected()) break; vTaskDelay(2); continue; }
      size_t amount = std::min(sizeof chunk, std::min(available, 24576 - total));
      size_t read = stream->readBytes(chunk, amount);
      if (f.write(chunk, read) != read) { f.close(); cacheFS->remove(temp); return false; }
      total += read; last = millis(); vTaskDelay(pdMS_TO_TICKS(4));
    }
    f.close(); if (total < 256 || (size >= 0 && total != size) || !current(t)) { cacheFS->remove(temp); return false; }
  }
  jpegFile = cacheFS->open(temp, FILE_READ);
  // Decoder workspace is ~18KB. Allocate it only after the TLS client dies.
  std::unique_ptr<JPEGDEC> jpeg(new(std::nothrow) JPEGDEC);
  if (!jpegFile || !jpeg || !jpeg->open(&jpegFile, jpegFile.size(), jpegClose, jpegRead, jpegSeek, drawJPEG)) { jpegFile.close(); cacheFS->remove(temp); return false; }
  sourceWidth = jpeg->getWidth(); sourceHeight = jpeg->getHeight();
  bool dimensions = sourceWidth > 0 && sourceHeight > 0 && sourceWidth <= 128 && sourceHeight <= 128;
  memset(staging, 0, vinyl::ART_BYTES);
  jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
  bool ok = dimensions && jpeg->decode(0, 0, 0);
  jpeg->close(); jpeg.reset(); jpegFile.close(); cacheFS->remove(temp);
  if (ok && current(t)) {
    publish(t, ResultKind::Artwork, "ARTWORK");
    writeCache(albumPath.length()?albumPath:path, vinyl::ART_BYTES);
  }
  return ok;
}
}

String cacheStem(const Track& t) {
  // Keep this legacy namespace to reuse existing LRC; old 64px art fails the length guard.
  String signature = String("v2-art64\n") + String(t.title) + "\n" + t.artist + "\n" + String(t.duration / 1000);
  uint8_t digest[32]; mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(signature.c_str()), signature.length(), digest, 0);
  char hex[33]; for (int i = 0; i < 16; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return String("/cache/") + hex;
}
// Share only verified catalog album artwork across tracks in that album.
// Separate key namespace preserves all existing per-song caches.
String albumCacheStem(const Track& t) {
  if (!t.album[0] || !t.artist[0]) return "";
  String signature = String("v2-album96\n") + t.album + "\n" + t.artist;
  uint8_t digest[32]; mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(signature.c_str()), signature.length(), digest, 0);
  char hex[33]; for (int i = 0; i < 16; ++i) snprintf(hex+i*2, 3, "%02x", digest[i]);
  return String("/cache/") + hex;
}
void networkTask(void*) {
  Track t;
  uint32_t loadedArtGeneration = 0;
  for (;;) {
    if (xQueueReceive(requests, &t, portMAX_DELAY) != pdTRUE) continue;
    storage::Lease storageUse;
    uint32_t lookupStarted = millis(); lookupStartedAt = lookupStarted;
    struct BackgroundWork {
      BackgroundWork() { runtimeBudget.background.store(true); }
      ~BackgroundWork() { runtimeBudget.background.store(false); }
    } backgroundWork;
    String title = cleaned(t.title); strlcpy(t.title, title.c_str(), sizeof t.title);
    String artist(t.artist); artist.replace(" - Topic", ""); artist.trim(); strlcpy(t.artist, artist.c_str(), sizeof t.artist);
    if (vinyl::advertisement(t.artist)) {
      publish(t, ResultKind::Status, "ADVERTISEMENT");
      Serial.println("[lookup] advertisement metadata; skip online lookup");
      continue;
    }
    String stem = cacheStem(t);
    String albumStem = albumCacheStem(t);
    bool lyricsCached = streamCache(t, stem + ".lrc", false);
    bool artCached = loadedArtGeneration == t.generation || streamCache(t, stem + ".rgb", true);
    if (!artCached && albumStem.length() && current(t)) {
      artCached = streamCache(t, albumStem + ".rgb", true);
      if (artCached) Serial.printf("[cache] gen=%u album artwork hit; skipping catalog and download\n", t.generation);
    }
    if (artCached) loadedArtGeneration = t.generation;
    if (lyricsCached && artCached) { Serial.printf("[cache] gen=%u all assets local; no lookup required\n", t.generation); continue; }
    if (!current(t)) continue;
    if (WiFi.status() != WL_CONNECTED) {
      if (!lyricsCached) publish(t, ResultKind::Status, "NO WIFI / NO CACHE");
      continue;
    }
    // SNTP is started at boot; wait cooperatively for certificate date checks.
    for (int i = 0; i < 80 && time(nullptr) < 1700000000 && current(t); ++i) vTaskDelay(pdMS_TO_TICKS(100));
    if (time(nullptr) < 1700000000) { if (!lyricsCached) publish(t, ResultKind::Status, "WAITING FOR CLOCK"); continue; }
    bool lyricRetrySoon = false;
    if (!lyricsCached && current(t)) {
      // Multi-artist credits rarely equal the catalogue's artist signature.
      // Structured search can provide a scored LRC in one response.
      bool collaborative = vinyl::primaryArtistLength(t.artist) < strlen(t.artist);
      bool done = false;
      auto route = vinyl::lyricRoute(collaborative,
        requestGate.remaining(vinyl::Service::LyricsGet, millis()), requestGate.remaining(vinyl::Service::LyricsSearch, millis()));
      if (route.wait) {
        requestFailure = "SERVICE BUSY"; requestRetryMs = retryDelay(route.wait);
      } else if (route.searchFirst) {
        int id = searchLyrics(t, stem + ".lrc");
        done = id == SEARCH_INLINE;
        if (!done && id >= 0 && current(t)) done = fetchLyrics(t, id, stem + ".lrc");
        if (!done && current(t) && (lastHTTP == 404 || lastHTTP == 503 || !lastHTTP))
          done = fetchLyrics(t, -1, stem + ".lrc");
      } else {
        done = fetchLyrics(t, -1, stem + ".lrc");
        if (!done && current(t) && (lastHTTP == 404 || lastHTTP == 503 || !t.artist[0])) {
          int id = searchLyrics(t, stem + ".lrc");
          done = id == SEARCH_INLINE;
          if (!done && id >= 0 && current(t)) done = fetchLyrics(t, id, stem + ".lrc");
        }
      }
      if (!done && current(t)) {
        // The first endpoint may become available before the fallback does.
        uint32_t getWait = requestGate.remaining(vinyl::Service::LyricsGet, millis());
        uint32_t searchWait = requestGate.remaining(vinyl::Service::LyricsSearch, millis());
        if (getWait && searchWait) requestRetryMs = retryDelay(std::min(getWait, searchWait));
        lyricRetrySoon = requestRetryMs <= 5000;
        publish(t, ResultKind::Status, requestFailure, requestRetryMs);
      }
    }
    if (!artCached && current(t) && !lyricRetrySoon && !uxQueueMessagesWaiting(requests)) {
      bool albumMatches = false;
      String url = findArtwork(t, albumMatches);
      if (!uxQueueMessagesWaiting(requests) && fetchArtwork(t, url, stem + ".rgb", albumMatches ? albumStem + ".rgb" : String()) && current(t)) loadedArtGeneration = t.generation;
    }
    Serial.printf("lookup complete elapsed=%ums heap=%u min=%u stack=%u\n", unsigned(millis()-lookupStarted), ESP.getFreeHeap(), ESP.getMinFreeHeap(), uxTaskGetStackHighWaterMark(nullptr));
  }
}
