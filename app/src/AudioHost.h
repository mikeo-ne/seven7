#pragma once
// seven7 — AudioHost: wires a juce::AudioDeviceManager to the engine Session.
//
// Threading contract (docs/01 ARC-RT-01..03):
//   • audioDeviceIOCallbackWithContext() is the RT thread. It only calls
//     Session::process(), which allocates nothing and takes no locks.
//   • Everything else (device changes, prepare) happens on the message thread.
//     A device restart re-prepares the Controller with the new rate/block, and
//     the Controller republishes its snapshot — the RT thread simply adopts it.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <functional>

#include "app/controller.h"

namespace s7::desktop {

class AudioHost final : private juce::AudioIODeviceCallback,
                        private juce::ChangeListener {
public:
  explicit AudioHost(app::Controller& controller);
  ~AudioHost() override;

  /// Opens the default device (2 in / 2 out) and starts streaming. Returns an
  /// error string (empty on success) exactly like AudioDeviceManager::initialise.
  juce::String start(int inputs = 2, int outputs = 2);
  void stop();

  juce::AudioDeviceManager& deviceManager() { return deviceManager_; }
  double currentSampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }
  int currentBlockSize() const { return blockSize_.load(std::memory_order_relaxed); }
  juce::String deviceName() const;

  /// Fired on the message thread after the device (re)started with new settings.
  std::function<void(double sampleRate, int blockSize)> onDeviceRestarted;

private:
  // AudioIODeviceCallback — RT thread
  void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                        float* const* outputChannelData, int numOutputChannels, int numSamples,
                                        const juce::AudioIODeviceCallbackContext& context) override;
  void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
  void audioDeviceStopped() override;
  void audioDeviceError(const juce::String& errorMessage) override;

  // ChangeListener — message thread (device list / settings changed)
  void changeListenerCallback(juce::ChangeBroadcaster*) override;

  app::Controller& controller_;
  juce::AudioDeviceManager deviceManager_;
  std::atomic<double> sampleRate_{48000.0};
  std::atomic<int> blockSize_{256};
  std::atomic<bool> running_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioHost)
};

}  // namespace s7::desktop
