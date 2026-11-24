#pragma once
#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include "lyrics_core.h"
#include "runtime_budget.h"
extern RuntimeBudget runtimeBudget;
struct Track {
  uint32_t generation = 0, duration = 0;
  char title[192]{}, artist[128]{}, album[128]{};
};
enum class ResultKind : uint8_t { Lyrics, Artwork, Status, LyricsStart, LyricsLine, LyricsEnd, LyricsAbort, ArtworkStart, ArtworkChunk, ArtworkEnd, ArtworkAbort };
struct Result { uint32_t generation; ResultKind kind; char status[40]; uint32_t retryAfterMs; uint16_t length, offset; };
extern std::atomic<uint32_t> generation;
extern QueueHandle_t requests, results, consumed;
extern fs::FS* cacheFS;
// One shared staging buffer, ownership passed by results/consumed queues.
// The network worker owns staging until the UI acknowledges each result.
extern uint8_t staging[vinyl::TRANSFER_LIMIT + 1];
void networkTask(void*);
String cacheStem(const Track& track);
