#include "AudioHost.h"

namespace s7::desktop {

AudioHost::AudioHost(app::Controller& controller) : controller_(controller) {
  deviceManager_.addChangeListener(this);
}

AudioHost::~AudioHost() {
  stop();
  deviceManager_.removeChangeListener(this);
}

juce::String AudioHost::start(int inputs, int outputs) {
  const juce::String err = deviceManager_.initialiseWithDefaultDevices(inputs, outputs);
  if (err.isNotEmpty()) return err;
  deviceManager_.addAudioCallback(this);
  return {};
}

void AudioHost::stop() {
  deviceManager_.removeAudioCallback(this);
  deviceManager_.closeAudioDevice();
}

juce::String AudioHost::deviceName() const {
  if (auto* d = deviceManager_.getCurrentAudioDevice()) return d->getName();
  return "No device";
}

// ── RT thread ────────────────────────────────────────────────────────────────

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                 float* const* outputChannelData, int numOutputChannels,
                                                 int numSamples, const juce::AudioIODeviceCallbackContext&) {
  if (!running_.load(std::memory_order_relaxed)) {
    for (int c = 0; c < numOutputChannels; ++c)
      if (outputChannelData[c] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[c], numSamples);
    return;
  }
  // Session::process overwrites the output and treats a null `in` as silence.
  controller_.session().process(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  const double sr = device->getCurrentSampleRate();
  const int block = device->getCurrentBufferSizeSamples();
  sampleRate_.store(sr, std::memory_order_relaxed);
  blockSize_.store(block, std::memory_order_relaxed);

  // Called on the message thread by JUCE before the callbacks begin (or on the
  // audio thread for some backends during a restart) — hand the re-prepare to
  // the message thread and keep the stream silent until it has run.
  running_.store(false, std::memory_order_relaxed);
  juce::MessageManager::callAsync([this, sr, block] {
    controller_.prepare(static_cast<std::int64_t>(sr), block);
    running_.store(true, std::memory_order_release);
    if (onDeviceRestarted) onDeviceRestarted(sr, block);
  });
}

void AudioHost::audioDeviceStopped() { running_.store(false, std::memory_order_relaxed); }

void AudioHost::audioDeviceError(const juce::String& errorMessage) {
  juce::MessageManager::callAsync([this, errorMessage] {
    if (controller_.on_log) controller_.on_log(("audio device error: " + errorMessage).toStdString());
  });
}

void AudioHost::changeListenerCallback(juce::ChangeBroadcaster*) {
  // Device settings dialog changed something; the device restarts itself and
  // audioDeviceAboutToStart re-prepares. Nothing else to do here.
}

}  // namespace s7::desktop
