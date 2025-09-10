#pragma once
#include <Arduino.h>
#include <string.h>
#include <AudioTools.h>

// Owns a PCM16 mono buffer in PSRAM and lets eSpeak write into it.
class MemoryBufferStream : public audio_tools::AudioStream {
public:
  MemoryBufferStream() : _pcm(nullptr), _capSamples(0), _sizeSamples(0) {}

  // Allocate/resize the internal buffer (capacity in SAMPLES, not bytes).
  // Returns false if allocation fails.
  bool begin(size_t capacitySamples) {
    end();
    _pcm = (int16_t*)ps_malloc(capacitySamples * sizeof(int16_t));
    if (!_pcm) return false;
    _capSamples  = capacitySamples;
    _sizeSamples = 0;
    return true;
  }

  // Free the buffer.
  void end() {
    if (_pcm) { free(_pcm); _pcm = nullptr; }
    _capSamples = _sizeSamples = 0;
  }

  void clear() { _sizeSamples = 0; }

  size_t write(const uint8_t* data, size_t len) override {
    if (!_pcm || !_capSamples || !len) return 0;
    size_t inSamples = len >> 1; 
    size_t room      = (_capSamples > _sizeSamples) ? (_capSamples - _sizeSamples) : 0;
    size_t toCopy    = inSamples < room ? inSamples : room;
    if (!toCopy) return 0;
    memcpy(_pcm + _sizeSamples, data, toCopy * sizeof(int16_t));
    _sizeSamples += toCopy;
    return toCopy * sizeof(int16_t);
  }

  // Accessors
  const int16_t* data() const { return _pcm; }
  size_t size()        const { return _sizeSamples; }
  size_t bytes()       const { return _sizeSamples * sizeof(int16_t); }
  size_t capacity()    const { return _capSamples; }
  size_t remaining()   const { return (_capSamples > _sizeSamples) ? (_capSamples - _sizeSamples) : 0; }

private:
  int16_t* _pcm;
  size_t   _capSamples;
  size_t   _sizeSamples;
};
