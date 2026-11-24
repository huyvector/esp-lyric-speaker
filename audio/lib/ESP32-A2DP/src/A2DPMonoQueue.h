#pragma once
#include <cstddef>
#include <cstdint>
namespace a2dp_mono {
// Called after the sink's existing volume/downmix stage made L == R.
// That stage already mutates the writable SDK PCM buffer. All stream readers
// have returned before write_audio(), and the base sink never reuses it after.
inline size_t packLeftInPlace(uint8_t* pcm, size_t stereoBytes) {
  if (!pcm || stereoBytes % 4) return 0;
  for (size_t sample = 0; sample < stereoBytes / 4; ++sample) {
    pcm[sample * 2] = pcm[sample * 4]; pcm[sample * 2 + 1] = pcm[sample * 4 + 1];
  }
  return stereoBytes / 2;
}
inline size_t expandStereo(const uint8_t* mono, size_t monoBytes, uint8_t* output, size_t capacity) {
  if (!mono || !output || monoBytes % 2 || monoBytes > capacity / 2) return 0;
  for (size_t i = 0; i < monoBytes; i += 2) {
    output[i * 2] = output[i * 2 + 2] = mono[i];
    output[i * 2 + 1] = output[i * 2 + 3] = mono[i + 1];
  }
  return monoBytes * 2;
}
}
