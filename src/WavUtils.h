#pragma once
#include <vector>
#include <stdint.h>
#include <string.h>

static void make_wav_from_pcm16(const int16_t* pcm, size_t samples,
                                uint32_t sample_rate,
                                std::vector<uint8_t>& wav_out) {
  const uint32_t data_bytes = samples * sizeof(int16_t);
  const uint32_t riff_size  = 36 + data_bytes;

  wav_out.resize(44 + data_bytes);
  uint8_t* p = wav_out.data();

  auto wr32 = [&](uint32_t v){ *p++=v&0xFF; *p++=(v>>8)&0xFF; *p++=(v>>16)&0xFF; *p++=(v>>24)&0xFF; };
  auto wr16 = [&](uint16_t v){ *p++=v&0xFF; *p++=(v>>8)&0xFF; };

  memcpy(p, "RIFF", 4); p += 4;                wr32(riff_size);
  memcpy(p, "WAVE", 4); p += 4;
  memcpy(p, "fmt ", 4); p += 4;                wr32(16);
  wr16(1);                                     // PCM
  wr16(1);                                     // mono
  wr32(sample_rate);
  wr32(sample_rate * 2);                       // byte rate
  wr16(2);                                     // block align
  wr16(16);                                    // bits/sample
  memcpy(p, "data", 4); p += 4;                wr32(data_bytes);

  memcpy(p, pcm, data_bytes);
}