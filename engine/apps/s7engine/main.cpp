// seven7 — s7engine: headless engine binary (spec: docs/01 §9, ARC-C01…C04)
//
// Runs the domain + engine layers without a UI or audio hardware:
// builds a small graph, computes PDC, renders deterministically through the
// NullAudioDevice, and prints a checksum. This is the backstop binary for CI
// conformance runs (bit-exact bounce diffs, codec round-trips).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <fstream>
#include <memory>
#include <string>

#include "domain/edit_engine.h"
#include "domain/time_model.h"
#include "engine/dsp_graph.h"
#include "engine/lane_scheduler.h"
#include "engine/mix_math.h"
#include "engine/session.h"
#include "pal/audio_device.h"
#include "pal/json.h"
#include "pal/project_codec.h"
#include "pal/wav_file.h"

namespace {

const char* arg_str(const char* name, const char* fallback, int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  return fallback;
}

std::uint64_t fnv1a(const std::vector<float>& data);
std::int64_t arg_int(const char* name, std::int64_t fallback, int argc, char** argv);

bool has_flag(const char* name, int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], name) == 0) return true;
  return false;
}

// ── bounce mode (MIX-13 / docs/01 §9) ─────────────────────────────────────────
//   s7engine bounce <Project.s7proj> [--out mix.wav] [--start smp] [--end smp] [--block n]
// Loads the bundle, compiles the render snapshot, renders offline and writes a
// 24-bit BWF. Prints an FNV-1a checksum of the float render for bit-exact CI diffs.
int run_bounce(int argc, char** argv) {
  const std::string bundle = argc > 2 ? argv[2] : "";
  if (bundle.empty()) { std::fprintf(stderr, "usage: s7engine bounce <Project.s7proj> [--out file.wav] [--start smp] [--end smp]\n"); return 2; }

  std::ifstream pf(bundle + "/project.json");
  if (!pf) { std::fprintf(stderr, "bounce: cannot read %s/project.json\n", bundle.c_str()); return 1; }
  const std::string text((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
  s7::pal::Json j;
  std::string err;
  if (!s7::pal::Json::parse(text, j, &err)) { std::fprintf(stderr, "bounce: bad JSON: %s\n", err.c_str()); return 1; }
  s7::domain::Project project;
  if (!s7::pal::project_from_json(j, project, &err)) { std::fprintf(stderr, "bounce: %s\n", err.c_str()); return 1; }

  s7::engine::SourceStore store;
  for (const auto& src : project.sources) {
    auto audio = std::make_shared<s7::engine::SourceAudio>();
    s7::pal::WavInfo info;
    if (s7::pal::read_wav(bundle + "/Media/Audio Files/" + src.name, *audio, info)) store.add(src.id, audio);
    else std::fprintf(stderr, "bounce: source '%s' offline (%s)\n", src.name.c_str(), info.error.c_str());
  }
  int slot = 0;
  for (auto& t : project.tracks) t.rt_slot = slot++;

  s7::engine::CompileReport rep;
  const s7::engine::SchedulerConfig sched{project.sample_rate, arg_int("--block", 64, argc, argv), 4096, 32768};
  auto snap = s7::engine::compile_snapshot(project, store, sched, &rep);
  for (const auto& w : rep.warnings) std::fprintf(stderr, "bounce: warning: %s\n", w.c_str());

  const std::int64_t start = arg_int("--start", 0, argc, argv);
  const std::int64_t end = arg_int("--end", project.content_end_sample(), argc, argv);
  const int block = static_cast<int>(arg_int("--block", 512, argc, argv));
  if (end <= start) { std::fprintf(stderr, "bounce: empty range\n"); return 1; }

  std::printf("bounce: '%s' %lld tracks, %lld regions, %lld..%lld @ %lld Hz, PDC worst %lld smp\n", project.name.c_str(),
              static_cast<long long>(project.tracks.size()), static_cast<long long>(project.regions.size()),
              static_cast<long long>(start), static_cast<long long>(end), static_cast<long long>(project.sample_rate),
              static_cast<long long>(rep.pdc_worst));
  const std::int64_t sr = snap->sample_rate;
  auto out = s7::engine::render_offline(std::move(snap), start, end, block);

  std::vector<float> interleaved;
  interleaved.reserve(out[0].size() * 2);
  for (std::size_t i = 0; i < out[0].size(); ++i) { interleaved.push_back(out[0][i]); interleaved.push_back(out[1][i]); }
  std::printf("bounce: %zu frames, checksum=0x%016llx\n", out[0].size(), static_cast<unsigned long long>(fnv1a(interleaved)));

  const std::string out_path = arg_str("--out", "", argc, argv);
  if (!out_path.empty()) {
    s7::engine::SourceAudio a;
    a.sample_rate = sr;
    a.ch = std::move(out);
    if (!s7::pal::write_wav(out_path, a, 24, start)) { std::fprintf(stderr, "bounce: cannot write %s\n", out_path.c_str()); return 1; }
    std::printf("bounce: wrote %s (24-bit BWF, time reference %lld)\n", out_path.c_str(), static_cast<long long>(start));
  }
  return 0;
}

constexpr double kTwoPi = 6.28318530717958647692;

std::int64_t arg_int(const char* name, std::int64_t fallback, int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], name) == 0) return std::atoll(argv[i + 1]);
  return fallback;
}

// FNV-1a over the rendered float stream — stable bounce identity for CI diffs.
std::uint64_t fnv1a(const std::vector<float>& data) {
  std::uint64_t h = 1469598103934665603ull;
  const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
  for (std::size_t i = 0; i < data.size() * sizeof(float); ++i) {
    h ^= bytes[i];
    h *= 1099511628211ull;
  }
  return h;
}

} // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "bounce") == 0) return run_bounce(argc, argv);
  if (has_flag("--help", argc, argv) || has_flag("-h", argc, argv)) {
    std::puts("s7engine — seven7 headless engine\n"
              "  s7engine [--sample-rate N] [--seconds N] [--buffer N]        smoke render + PDC report\n"
              "  s7engine bounce <Project.s7proj> [--out mix.wav] [--start smp] [--end smp] [--block n]");
    return 0;
  }
  const std::int64_t sample_rate = arg_int("--sample-rate", 48000, argc, argv);
  const std::int64_t seconds = arg_int("--seconds", 1, argc, argv);
  const std::int64_t buffer = arg_int("--buffer", 64, argc, argv);

  std::printf("s7engine 0.9.0 — seven7 headless engine\n");
  std::printf("sample_rate=%lld block=%lld seconds=%lld\n",
              static_cast<long long>(sample_rate), static_cast<long long>(buffer),
              static_cast<long long>(seconds));

  // --- Domain: time model (ARC-TIME) --------------------------------------
  s7::domain::TempoMap tempo{sample_rate, {120, 1}};
  tempo.append_segment(1920 * 4, {60, 1});
  std::printf("tempo map: 2 segments, bar 5 seam @ sample %lld\n",
              static_cast<long long>(tempo.tick_to_sample(1920 * 4)));

  // --- Engine: hybrid buffer classification (ARC-ENG-01) -------------------
  s7::engine::LaneScheduler scheduler{{sample_rate, buffer, 4096, 32768}};
  const auto lane_armed = scheduler.classify({.record_armed = true}, false);
  const auto lane_parked = scheduler.classify({}, false);
  std::printf("lanes: armed track -> %s | parked track -> %s | L_ahead=%lld samples\n",
              lane_armed == s7::engine::Lane::kRt ? "RT" : "MAE",
              lane_parked == s7::engine::Lane::kRt ? "RT" : "MAE",
              static_cast<long long>(scheduler.look_ahead(1152)));

  // --- Engine: graph + delay compensation (MIX-05, MIX-07) ------------------
  s7::engine::DspGraph graph;
  const auto drums = graph.add_node("Drums", 0, 0.9);
  const auto keys = graph.add_node("Keys", 0, 0.8);
  const auto lineq = graph.add_node("LinearEQ(insert)", 1152, 1.0);
  const auto mix = graph.add_node("MixBus");
  graph.add_edge(drums, mix);
  graph.add_edge(keys, lineq);
  graph.add_edge(lineq, mix);

  const auto pdc = graph.compensate(mix, 32768);
  std::printf("PDC: domain=%lld samples\n", static_cast<long long>(pdc.domain_delay));
  for (const auto& e : pdc.inserted)
    if (e.inserted_samples > 0)
      std::printf("  insert %lld spl on %s -> %s\n",
                  static_cast<long long>(e.inserted_samples),
                  graph.node(e.from).name.c_str(), graph.node(e.to).name.c_str());

  // --- Engine: offline render through the PAL (ARC-C01) ---------------------
  s7::pal::NullAudioDevice device;
  device.open({sample_rate, 2, static_cast<int>(buffer)});

  std::vector<float> render;
  const double f1 = 55.0, f2 = 220.0;
  const double g1 = graph.node(drums).gain, g2 = graph.node(keys).gain;
  std::int64_t phase = 0;
  device.start([&](const float*, float* out, int n, int ch) {
    for (int i = 0; i < n; ++i) {
      const double t = static_cast<double>(phase + i) / static_cast<double>(sample_rate);
      const double s =
          g1 * std::sin(kTwoPi * f1 * t) + g2 * std::sin(kTwoPi * f2 * t);
      for (int c = 0; c < ch; ++c) out[i * ch + c] = static_cast<float>(s);
    }
    phase += n;
    render.insert(render.end(), out, out + static_cast<std::size_t>(n) * 2);
  });
  device.pump(static_cast<int>(sample_rate * seconds));
  device.stop();

  std::printf("rendered %lld frames via NullAudioDevice, checksum=0x%016llx\n",
              static_cast<long long>(phase),
              static_cast<unsigned long long>(fnv1a(render)));
  std::puts("OK");
  return 0;
}
