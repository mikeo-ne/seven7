#pragma once
// seven7 — Platform layer (PAL): audio device abstraction (spec: docs/01 ARC-C01)
//
// Real backends (CoreAudio on macOS, ASIO + WASAPI-exclusive on Windows) plug
// into this interface. NullAudioDevice drives the same callback from pump() so
// the engine is fully exercisable — and CI-testable — without audio hardware.

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace s7::pal {

struct AudioFormat {
  std::int64_t sample_rate = 48000;
  int channels = 2;
  int block_size = 64; // hardware buffer, samples per channel
};

/// Pull-style I/O: the engine fills `out` and may read `in` (interleaved, float).
using AudioCallback =
    std::function<void(const float* input, float* output, int frames, int channels)>;

class IAudioDevice {
public:
  virtual ~IAudioDevice() = default;
  virtual bool open(const AudioFormat& format) = 0;
  virtual bool start(AudioCallback callback) = 0; // ARC-RT-01 rules apply inside callback
  virtual void stop() = 0;
};

class NullAudioDevice final : public IAudioDevice {
public:
  bool open(const AudioFormat& format) override {
    format_ = format;
    input_.assign(static_cast<std::size_t>(format.block_size) * format.channels, 0.0f);
    output_.assign(static_cast<std::size_t>(format.block_size) * format.channels, 0.0f);
    return true;
  }

  bool start(AudioCallback callback) override {
    callback_ = std::move(callback);
    running_ = true;
    return true;
  }

  void stop() override { running_ = false; }

  /// Drives one or more hardware-sized blocks through the callback (test/CI path).
  int pump(int frames) {
    if (!running_ || !callback_) return 0;
    int done = 0;
    while (done < frames) {
      const int n = std::min(format_.block_size, frames - done);
      callback_(input_.data(), output_.data(), n, format_.channels);
      done += n;
    }
    return done;
  }

  bool running() const { return running_; }
  const std::vector<float>& last_output() const { return output_; }

private:
  AudioFormat format_;
  AudioCallback callback_;
  std::vector<float> input_;
  std::vector<float> output_;
  bool running_ = false;
};

} // namespace s7::pal
