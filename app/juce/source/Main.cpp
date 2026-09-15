/* ═══════════════════════════════════════════════════════════════════
   seven7 — native DAW shell (JUCE)
   ─────────────────────────────────────────────────────────────────
   A working vertical slice of the seven7 DAW for desktop:
     · Real I/O through the OS audio device (JUCE AudioAppComponent)
     · The audio callback runs the repository engine kernels:
         s7::engine::fader_db / db_to_gain   → strip fader (MIX-03)
         s7::engine::constant_power_pan      → panner (MIX-10)
         s7::engine::accumulate_bus/finalize → 64-bit master sum (ARC-T03)
         s7::domain::TempoMap                → position LCD (ARC-TIME)
       (TempoMap requires __int128: GCC/Clang. On MSVC the shell falls
       back to exact single-tempo integer math — see BUILD-NATIVE.md.)
     · Monitor → fader → pan → record → save take as WAV
     · ARC-RT-01 honoured: no heap allocation, no locks, no I/O in the
       audio callback (position is an atomic; takes buffer is
       pre-allocated in prepareToPlay).
   ═══════════════════════════════════════════════════════════════════ */

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <vector>

#include "engine/mix_math.h"

#if defined(__SIZEOF_INT128__)
#include "domain/time_model.h"
#define S7_HAS_TIMEMODEL 1
#else
#define S7_HAS_TIMEMODEL 0
#endif

/* ─── colours (Pro Tools look) ──────────────────────────────────── */
namespace ui {
constexpr juce::Colour bg{ 0xff2c2e33 };
constexpr juce::Colour bgInset{ 0xff1b1d20 };
constexpr juce::Colour raise{ 0xff383b41 };
constexpr juce::Colour ink1{ 0xffe8eaef };
constexpr juce::Colour ink2{ 0xffa9aeb9 };
constexpr juce::Colour orange{ 0xffff8c1a };
constexpr juce::Colour orangeText{ 0xffffb454 };
constexpr juce::Colour rec{ 0xffff4b3e };
constexpr juce::Colour ok{ 0xff3ddc84 };
}

/* ─── engine (audio thread) ─────────────────────────────────────── */
class S7NativeEngine : public juce::AudioIODeviceCallback {
public:
  /* control-side state (atomics: no locks in the callback, ARC-RT-01) */
  std::atomic<bool> playing{ false };
  std::atomic<bool> recording{ false };
  std::atomic<bool> toneOn{ false };
  std::atomic<bool> muted{ false };
  std::atomic<double> faderPos{ 0.75 }; /* 0 dB unity (MIX-03) */
  std::atomic<double> panVal{ 0.0 };
  std::atomic<double> trimDb{ 0.0 };
  std::atomic<int64_t> posSamples{ 0 };
  std::atomic<float> peak{ 0.0f };      /* written in callback, read by UI */
  std::atomic<int64_t> milliBpm{ 120000 }; /* milli-BPM, den = 1000 (rational-safe) */
  std::atomic<bool> ready{ false };

  int sampleRate = 48000;
  int blockSize = 64;

  /* pre-allocated recording (filled in prepareToPlay) */
  juce::AudioBuffer<float> rec;
  int64_t recMax = 0;
  int64_t recPos = 0;
  int64_t recLen = 0;

  void prepareToPlay(int samplesPerBlockSpec, double sampleRateSpec) override {
    blockSize = samplesPerBlockSpec;
    sampleRate = (int) sampleRateSpec;
    accL.assign((size_t) samplesPerBlockSpec, 0.0);
    accR.assign((size_t) samplesPerBlockSpec, 0.0);
    procL.resize((size_t) samplesPerBlockSpec);
    procR.resize((size_t) samplesPerBlockSpec);
    /* up to ~5 minutes of stereo 32-bit float takes */
    recMax = (int64_t) sampleRateSpec * 300;
    rec.setSize(2, (int) recMax, false, false, true);
    tonePhase = 0.0;
    recPos = 0;
    recLen = 0;
    ready = true;
  }

  void audioDeviceIOCallback(const float* input, float* output, int numSamples, int numChannels) override {
    const int n = numSamples;
    const float* inL = input;
    const float* inR = (input && numChannels > 1) ? input + 1 : input;

    /* engine kernels (docs/03) */
    const double trim = s7::engine::db_to_gain(trimDb.load());
    const double fader = s7::engine::db_to_gain(s7::engine::fader_db(faderPos.load()));
    const auto pan = s7::engine::constant_power_pan(panVal.load());
    const bool mute = muted.load();
    const bool recNow = recording.load();
    const bool tone = toneOn.load();
    const double toneStep = (2.0 * juce::MathConstants<double>::pi * 440.0) / (double) sampleRate;

    float pk = 0.0f;
    for (int i = 0; i < n; ++i) {
      float l = (inL != nullptr) ? inL[i] : 0.0f;
      float r = (inR != nullptr) ? inR[i] : 0.0f;
      l *= (float) trim;
      r *= (float) trim;

      /* record tap: post-trim, pre-fader (PT convention) */
      if (recNow && recPos < recMax) {
        rec.setSample(0, (int) recPos, l);
        rec.setSample(1, (int) recPos, r);
        recPos += 1;
      }

      if (tone) {
        const float s = (float) std::sin(tonePhase) * 0.2f;
        l += s;
        r += s;
      }
      tonePhase += toneStep;

      if (mute) {
        procL[(size_t) i] = 0.0f;
        procR[(size_t) i] = 0.0f;
      } else {
        procL[(size_t) i] = l * (float) fader * (float) pan.left;
        procR[(size_t) i] = r * (float) fader * (float) pan.right;
      }
      const float al = std::abs(procL[(size_t) i]);
      const float ar = std::abs(procR[(size_t) i]);
      if (al > pk) pk = al;
      if (ar > pk) pk = ar;
    }

    /* ARC-T03: 64-bit summing junction, single round at the output stage */
    std::fill(accL.begin(), accL.begin() + n, 0.0);
    std::fill(accR.begin(), accR.begin() + n, 0.0);
    s7::engine::accumulate_bus(procL.data(), accL.data(), (size_t) n, 1.0);
    s7::engine::accumulate_bus(procR.data(), accR.data(), (size_t) n, 1.0);
    s7::engine::finalize_bus(accL.data(), output, (size_t) n);
    s7::engine::finalize_bus(accR.data(), (numChannels > 1) ? output + 1 : output, (size_t) n);

    peak.store(pk);
    if (playing.load()) posSamples.fetch_add(n);
  }

  void startRecording() {
    recPos = 0;
    recLen = 0;
    rec.clear();
    recording = true;
  }
  void stopRecording() {
    recording = false;
    recLen = recPos;
  }

private:
  std::vector<double> accL, accR;
  std::vector<float> procL, procR;
  double tonePhase = 0.0;
};

/* ─── LCD: Pro Tools-style amber position readout ───────────────── */
class LcdComponent : public juce::Component {
public:
  juce::Font font{ 22.0f, juce::Font::plain };

  void paint(juce::Graphics& g) override {
    g.fillAll(ui::bgInset);
    g.setColour(ui::orangeText);
    g.setFont(font);
    g.drawText(text, getLocalBounds(), juce::Justification::centred, false);
    g.setColour(juce::Colours::black);
    g.drawRect(getLocalBounds());
  }
  void setText(const juce::String& t) { text = t; repaint(); }

private:
  juce::String text{ "1 1 1 00" };
};

/* ─── level meter ───────────────────────────────────────────────── */
class MeterComponent : public juce::Component {
public:
  void setEngine(S7NativeEngine* e) { engine = e; }
  void paint(juce::Graphics& g) override {
    g.fillAll(ui::bgInset);
    const float level = (engine != nullptr) ? engine->peak.load() : 0.0f;
    if (level > 0.0f) {
      display = std::max((float) (juce::jlimit(0.0, 60.0, 20.0 * std::log10(std::max(level, 1e-7f)))),
                         display * 0.86f);
    } else {
      display *= 0.86f;
    }
    const float db = juce::jlimit(-60.0f, 0.0f, display);
    const float frac = (db + 60.0f) / 60.0f;
    const juce::Rectangle<int> r = getLocalBounds().reduced(1);
    const int h = (int) (frac * (float) r.getHeight());
    for (int y = r.getBottom() - h; y < r.getBottom(); ++y) {
      const float f = (float) (r.getBottom() - y) / (float) r.getHeight();
      juce::Colour c;
      if (f < 0.78f) c = ui::ok;
      else if (f < 0.92f) c = juce::Colour(0xffffd60a);
      else c = ui::rec;
      g.fillRect(r.getX(), y, r.getWidth(), 1, c);
    }
    g.drawRect(getLocalBounds());
  }
  void timerCallback() override { repaint(); }

private:
  S7NativeEngine* engine = nullptr;
  float display = 0.0f;
};

/* ─── top bar: PT-style transport + LCD ─────────────────────────── */
class TopBar : public juce::Component {
public:
  juce::TextButton stop{ "⏹" }, play{ "▶" }, rec{ "●" };
  LcdComponent lcdPos, lcdBpm;
  juce::TextButton bpmApply{ "BPM" };
  juce::TextEditor bpmEdit;
  juce::TextButton settings{ "⚙ Settings" };
  juce::Label status;

  TopBar() {
    stop.setFont(juce::Font(16.0f));
    play.setFont(juce::Font(16.0f));
    rec.setFont(juce::Font(16.0f));
    rec.setButtonText("●");
    bpmEdit.setText("120.000", false);
    bpmEdit.setFont(juce::Font(12.0f));
    status.setFont(juce::Font(11.0f));
    status.setColour(juce::Label::textColourId, ui::ink2);
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(ui::bg);
    g.setColour(juce::Colours::black);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
  }

  void layout(int w, int h) {
    const int pad = 12;
    int x = pad;
    auto place = [&](juce::Component& c, int ww, int hh, int yy) {
      c.setBounds(x, yy, ww, hh);
      x += ww + 8;
    };
    const int mid = (h - 26) / 2;
    place(stop, 34, 26, mid);
    place(play, 34, 26, mid);
    place(rec, 34, 26, mid);
    lcdPos.setBounds(x, (h - 34) / 2, 170, 34);
    x += 178;
    lcdBpm.setBounds(x, (h - 30) / 2, 84, 30);
    x += 92;
    bpmEdit.setBounds(x, (h - 22) / 2, 76, 22);
    x += 84;
    place(bpmApply, 40, 22, (h - 22) / 2);
    place(settings, 110, 24, (h - 24) / 2);
    status.setBounds(w - 420 - pad, (h - 18) / 2, 420, 18);
    status.setJustificationType(juce::Justification::right);
  }
};

/* ─── main content ──────────────────────────────────────────────── */
class MainContent : public juce::Component, public juce::Timer {
public:
  MainContent(S7NativeEngine& e, juce::AudioDeviceSelectorComponentListener& listener)
      : engine(e), deviceListener(listener) {
    lookAndFeel.setBackgroundColour(ui::bg);
    lookAndFeel.findColour(juce::Button::buttonOnColourId) = ui::raise;

    topBar.play.onClick = [this] { engine.playing = true; };
    topBar.stop.onClick = [this] {
      engine.playing = false;
      engine.posSamples = 0;
    };
    topBar.rec.onClick = [this] {
      if (engine.recording.load()) {
        engine.stopRecording();
        saveBtn.setEnabled(true);
        toast("Take recorded — " + juce::String((double) engine.recLen / (double) engine.sampleRate) + " s");
      } else {
        engine.startRecording();
        saveBtn.setEnabled(false);
        toast("Recording…");
      }
    };
    topBar.bpmApply.onClick = [this] {
      const double v = topBar.bpmEdit.getText().getDoubleValue();
      if (v >= 20.0 && v <= 300.0) setBpm((int64_t) juce::roundToInt(v * 1000.0));
    };
    topBar.settings.onClick = [this] {
      openSettings();
    };
    saveBtn.onClick = [this] { saveTake(); };
    saveBtn.setEnabled(false);
    toneBtn.setClickingTogglesState(true);
    toneBtn.onClick = [this] { engine.toneOn = toneBtn.getToggledState(); };

    /* sliders */
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setRange(0.0, 1.0, 0.001);
    fader.setValue(0.75, juce::dontSendNotification);
    fader.onValueChange = [this] { engine.faderPos = fader.getValue(); };
    fader.setTextboxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    fader.setTextValueSuffix(" pos");

    pan.setSliderStyle(juce::Slider::LinearHorizontal);
    pan.setRange(-1.0, 1.0, 0.01);
    pan.setValue(0.0, juce::dontSendNotification);
    pan.onValueChange = [this] { engine.panVal = pan.getValue(); };
    pan.setTextboxStyle(juce::Slider::TextBoxBelow, false, 56, 16);

    trim.setSliderStyle(juce::Slider::LinearHorizontal);
    trim.setRange(-24.0, 24.0, 0.1);
    trim.setValue(0.0, juce::dontSendNotification);
    trim.onValueChange = [this] { engine.trimDb = trim.getValue(); };
    trim.setTextboxStyle(juce::Slider::TextBoxBelow, false, 56, 16);

    faderLabel.setText("Fader (MIX-03)", juce::dontSendNotification);
    panLabel.setText("Pan (MIX-10)", juce::dontSendNotification);
    trimLabel.setText("Input trim ±24 dB (MIX-01)", juce::dontSendNotification);
    faderLabel.setJustificationType(juce::Justification::centred);
    panLabel.setJustificationType(juce::Justification::centred);
    trimLabel.setJustificationType(juce::Justification::centred);
    kernelNote.setText(
#if S7_HAS_TIMEMODEL
        "engine kernels: MIX-03 fader · MIX-10 pan · ARC-T03 64-bit sum · ARC-TIME tempo map",
#else
        "engine kernels: MIX-03 fader · MIX-10 pan · ARC-T03 64-bit sum · single-tempo fallback (MSVC)",
#endif
        juce::dontSendNotification);
    kernelNote.setColour(juce::Label::textColourId, ui::ink2);
    faderLabel.setColour(juce::Label::textColourId, ui::ink2);
    panLabel.setColour(juce::Label::textColourId, ui::ink2);
    trimLabel.setColour(juce::Label::textColourId, ui::ink2);

    meter.setEngine(&engine);
    meter.startTimerHz(45);
    startTimerHz(30);
  }

  void setBpm(int64_t milliBpm) {
    engine.milliBpm = milliBpm;
#if S7_HAS_TIMEMODEL
    const int64_t pos = engine.posSamples.load();
    if (timeModel == nullptr || modelSr != engine.sampleRate || pos == 0) {
      timeModel = std::make_unique<s7::domain::TempoMap>(engine.sampleRate, { milliBpm, 1000 });
      modelSr = engine.sampleRate;
    } else {
      timeModel->append_segment(timeModel->sample_to_tick(pos), { milliBpm, 1000 });
    }
#endif
  }

  void paint(juce::Graphics& g) override {
    g.fillAll(ui::bg);
    if (juce::Time::getMillisecondCounter() < toastUntil && !toastText.isEmpty()) {
      g.setColour(ui::orangeText);
      g.setFont(juce::Font(12.0f));
      g.drawText(toastText, getLocalBounds().withHeight(24),
                 juce::Justification::centred, false);
    }
  }

  void resized() override {
    const int w = getWidth(), h = getHeight();
    topBar.setBounds(0, 0, w, 64);
    topBar.layout(w, 64);

    int y = 84;
    trimLabel.setBounds(120, y, 220, 16);
    trim.setBounds(110, y + 18, 240, 22);
    y += 64;
    faderLabel.setBounds(120, y, 220, 16);
    fader.setBounds(150, y + 18, 60, 170);
    panLabel.setBounds(300, y, 220, 16);
    pan.setBounds(300, y + 18, 240, 22);
    y += 210;

    meter.setBounds(160, y, 14, 150);
    toneBtn.setBounds(200, y + 40, 150, 28);
    saveBtn.setBounds(200, y + 78, 150, 28);
    kernelNote.setBounds(120, y + 118, 620, 16);
  }

  void timerCallback() override {
    const int64_t pos = engine.posSamples.load();
#if S7_HAS_TIMEMODEL
    if (timeModel == nullptr || modelSr != engine.sampleRate) {
      timeModel = std::make_unique<s7::domain::TempoMap>(engine.sampleRate,
                                                         { engine.milliBpm.load(), 1000 });
      modelSr = engine.sampleRate;
    }
#endif
    juce::String pstr;
#if S7_HAS_TIMEMODEL
    if (timeModel != nullptr) {
      const int64_t tick = timeModel->sample_to_tick(pos);
      const int64_t bar = tick / 3840 + 1;
      const int64_t beat = (tick % 3840) / 960 + 1;
      const int64_t frame = (tick % 960) / 960 * 100;
      pstr = juce::String(bar).paddedLeft('0', 3) + "  " + juce::String(beat) + "  "
             + juce::String(frame).paddedLeft('0', 2);
    }
#else
    {
      const double bpm = (double) engine.milliBpm.load() / 1000.0;
      const double beats = (double) pos * bpm / (60.0 * (double) engine.sampleRate);
      const int64_t bar = (int64_t) (beats / 4.0) + 1;
      const int64_t beat = (int64_t) (beats - std::floor(beats / 4.0) * 4.0) + 1;
      const int64_t frame = (int64_t) ((beats - std::floor(beats)) * 100.0);
      pstr = juce::String(bar).paddedLeft('0', 3) + "  " + juce::String(beat) + "  "
             + juce::String(frame).paddedLeft('0', 2);
    }
#endif
    topBar.lcdPos.setText(pstr);
    topBar.lcdBpm.setText(juce::String((int) (engine.milliBpm.load() / 1000)) + "."
                          + juce::String((int) (engine.milliBpm.load() % 1000)).paddedLeft('0', 3));
    topBar.status.setText(
      juce::String(engine.sampleRate) + " Hz · " + juce::String(engine.blockSize)
      + " smp buf" + (engine.recording.load() ? " · ● REC" : "")
      + (engine.playing.load() ? " · playing" : ""),
      juce::dontSendNotification);
  }

  void openSettings() {
    if (settingsWin == nullptr) {
      deviceComp = std::make_shared<juce::AudioDeviceSelectorComponent>(deviceListener);
      settingsWin = std::make_unique<juce::DocumentWindow>(
          "seven7 — Audio Device", ui::ink1,
          juce::DocumentWindow::allButtons, true);
      settingsWin->setContentComponent(deviceComp.get(), true);
      settingsWin->setResizable(true, false);
      settingsWin->setSize(560, 420);
      settingsWin->centreWithSize(560, 420);
      settingsWin->setVisible(true);
    } else {
      settingsWin->toFront(true);
    }
  }

  void saveTake() {
    if (engine.recLen <= 0) return;
    juce::FileChooser chooser("Save take as WAV…",
                              juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
                              "*.wav");
    if (chooser.browseForFileToSave()) {
      juce::File f = chooser.getResult();
      if (!f.hasFileExtension("wav")) f = f.withFileExtension("wav");
      auto stream = std::make_unique<juce::FileOutputStream>(f);
      if (stream != nullptr && stream->isOpen()) {
        auto wav = std::make_shared<juce::WAVAudioFormat>();
        auto writer = wav->createWriter(
            stream.get(), (double) engine.sampleRate, 2, 24, {});
        if (writer != nullptr) {
          const int64_t n = engine.recLen;
          /* copy to a temp buffer on the UI thread (allocation OK here) */
          juce::AudioBuffer<float> tmp(2, (int) n);
          for (int64_t i = 0; i < n; ++i) {
            tmp.setSample(0, (int) i, engine.rec.getSample(0, (int) i));
            tmp.setSample(1, (int) i, engine.rec.getSample(1, (int) i));
          }
          writer->writeFromAudioSampleBuffer(tmp, 0, (int) n);
          toast("Saved " + f.getFileName());
        } else {
          toast("WAV writer failed");
        }
      }
    }
  }

  void toast(const juce::String& msg) {
    toastText = msg;
    toastUntil = juce::Time::getMillisecondCounter() + 3000;
    repaint();
  }

  S7NativeEngine& engine;
  juce::LookAndFeel_V4 lookAndFeel;
  TopBar topBar;
  juce::Slider fader, pan, trim;
  juce::Label faderLabel, panLabel, trimLabel, kernelNote;
  juce::TextButton saveBtn{ "Save take → WAV…" }, toneBtn{ "Test tone" };
  MeterComponent meter;
#if S7_HAS_TIMEMODEL
  std::unique_ptr<s7::domain::TempoMap> timeModel;
#endif
  int modelSr = -1;
  std::shared_ptr<juce::AudioDeviceSelectorComponent> deviceComp;
  std::unique_ptr<juce::DocumentWindow> settingsWin;
  juce::AudioDeviceSelectorComponentListener& deviceListener;
  juce::String toastText;
  int64_t toastUntil = 0;
};

/* ─── application ───────────────────────────────────────────────── */
class Seven7App : public juce::JUCEApplication, public juce::AudioAppComponent,
                 public juce::AudioDeviceSelectorComponentListener {
public:
  Seven7App() : engine(), content(engine, *this) {
  }

  void initialise(const juce::String&) override {
    window = std::make_unique<juce::DocumentWindow>(
        "seven7 — Session 1", ui::ink1,
        juce::DocumentWindow::allButtons, true);
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, true);
    window->setContentComponent(&content, true);
    window->setLookAndFeel(&content.lookAndFeel);
    window->setSize(980, 560);
    window->centreWithSize(980, 560);
    window->setVisible(true);
    startAudio();
  }

  void juceApplicationSystemShutdown() override { stopAudio(); }
  void shutdown() override {
    window = nullptr;
  }

  bool moreTimeNeededIsIdle() override { return true; }

  void prepareToPlay(int s, double r) override { engine.prepareToPlay(s, r); }
  void audioDeviceIOCallback(const float* in, float* out, int n, int c) override {
    engine.audioDeviceIOCallback(in, out, n, c);
  }

  void audioDeviceSelectorChanged() override { startAudio(); }

  S7NativeEngine engine;
  MainContent content;
  std::unique_ptr<juce::DocumentWindow> window;
};

START_JUCE_APPLICATION(Seven7App)
