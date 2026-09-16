// seven7 — codec tests: WAV/BWF (ARC-C11, MIX-13), JSON, `.s7proj` round-trip (ARC-T05)

#include <cmath>
#include <cstdint>

#include "domain/edit_engine.h"
#include "pal/json.h"
#include "pal/project_codec.h"
#include "pal/wav_file.h"
#include "s7_test.h"

using namespace s7;

namespace {

void wav_round_trip_24bit_and_float() {
  engine::SourceAudio a;
  a.sample_rate = 48000;
  a.ch.assign(2, std::vector<float>(1000));
  for (int i = 0; i < 1000; ++i) {
    a.ch[0][static_cast<std::size_t>(i)] = std::sin(i * 0.05f) * 0.8f;
    a.ch[1][static_cast<std::size_t>(i)] = -std::sin(i * 0.05f) * 0.3f;
  }
  for (int bits : {24, 32}) {
    const auto bytes = pal::encode_wav(a, bits, 123456789012LL, "take 1");
    engine::SourceAudio b;
    pal::WavInfo info;
    S7_CHECK(pal::decode_wav(bytes, b, info));
    S7_CHECK(info.error.empty());
    S7_CHECK(info.channels == 2 && info.sample_rate == 48000 && info.frames == 1000 && info.bits == bits);
    S7_CHECK(info.is_float == (bits == 32));
    S7_CHECK(info.time_reference == 123456789012LL);   // BWF time reference survives (spotting)
    double max_err = 0;
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < 1000; ++i)
        max_err = std::max(max_err, static_cast<double>(std::fabs(a.ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] - b.ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)])));
    S7_CHECK(max_err <= (bits == 32 ? 0.0 : 0.5 / 8388608.0 + 1e-7));  // ≤ ½ LSB after rounding
  }
  // 16-bit PCM decode path
  std::vector<unsigned char> w;
  auto push32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<unsigned char>(v >> (8 * i))); };
  auto push16 = [&](std::uint16_t v) { w.push_back(static_cast<unsigned char>(v)); w.push_back(static_cast<unsigned char>(v >> 8)); };
  w.insert(w.end(), {'R', 'I', 'F', 'F'}); push32(0); w.insert(w.end(), {'W', 'A', 'V', 'E'});
  w.insert(w.end(), {'f', 'm', 't', ' '}); push32(16); push16(1); push16(1); push32(44100); push32(88200); push16(2); push16(16);
  w.insert(w.end(), {'L', 'I', 'S', 'T'}); push32(3); w.insert(w.end(), {'a', 'b', 'c', 0});  // odd-length chunk + pad
  w.insert(w.end(), {'d', 'a', 't', 'a'}); push32(6); push16(0); push16(16384); push16(static_cast<std::uint16_t>(-32768));
  engine::SourceAudio c;
  pal::WavInfo info;
  S7_CHECK(pal::decode_wav(w, c, info));
  S7_CHECK(info.frames == 3 && info.sample_rate == 44100 && info.bits == 16);
  S7_CHECK_NEAR(c.ch[0][1], 0.5, 1e-6);
  S7_CHECK_NEAR(c.ch[0][2], -1.0, 1e-6);
  std::vector<unsigned char> garbage{'n', 'o', 'p', 'e'};
  S7_CHECK(!pal::decode_wav(garbage, c, info));
  S7_CHECK(!info.error.empty());
}

void json_parse_and_dump() {
  pal::Json j;
  std::string err;
  S7_CHECK(pal::Json::parse(R"({"a":[1,2.5,-3,"x\ny\u00e9"],"b":{"c":true,"d":null},"big":9007199254740993})", j, &err));
  S7_CHECK(err.empty());
  S7_CHECK(j["a"].size() == 4);
  S7_CHECK(j["a"].at(0).as_int() == 1);
  S7_CHECK_NEAR(j["a"].at(1).as_double(), 2.5, 0);
  S7_CHECK(j["a"].at(2).as_int() == -3);
  S7_CHECK(j["a"].at(3).as_string() == "x\ny\xC3\xA9");
  S7_CHECK(j["b"]["c"].as_bool());
  S7_CHECK(j["b"]["d"].is_null());
  S7_CHECK(j["big"].as_int() == 9007199254740993LL);  // 64-bit integers are exact (sample positions!)
  S7_CHECK(j["missing"]["deep"].is_null());
  const std::string text = j.dump();
  pal::Json k;
  S7_CHECK(pal::Json::parse(text, k, &err));
  S7_CHECK(k.dump() == text);
  S7_CHECK(!pal::Json::parse("{\"a\":}", k, &err));
  S7_CHECK(!pal::Json::parse("[1,2", k, &err));
  S7_CHECK(!pal::Json::parse("{} x", k, &err));
}

void project_round_trip() {
  domain::Project p;
  p.name = "Midnight City";
  p.sample_rate = 96000;
  p.tempo = {120005, 1000};  // 120.005 BPM as an exact rational
  p.sig_num = 7; p.sig_den = 8; p.key = "D♭";
  domain::Source s; s.id = p.allocate_id(); s.name = "vox.wav"; s.length_samples = 1LL << 40; s.channels = 2; s.sample_rate = 96000;
  p.sources.push_back(s);
  const domain::Id vox = domain::add_track(p, domain::TrackKind::kAudio, "Vox Ld", 5);
  const domain::Id bus = domain::add_track(p, domain::TrackKind::kBus, "VoxBus", 7);
  const domain::Id keys = domain::add_track(p, domain::TrackKind::kInstrument, "Keys", 9);
  domain::add_track(p, domain::TrackKind::kMaster, "Master");
  domain::Track* t = p.find_track(vox);
  t->output = bus; t->fader_pos = 0.6125; t->pan = -0.25; t->mute = true; t->arm = true;
  t->inserts[0] = {"S7 EQ-8", false, 0}; t->inserts[1] = {"S7 Comp", true, 64};
  t->sends[1] = {bus, -6.5, true, false, true};
  p.find_track(keys)->instrument = 2;
  domain::Region r; r.id = p.allocate_id(); r.track_id = vox; r.source_id = s.id; r.name = "vox 01 \"comp\"";
  r.start_sample = 123456789012345LL; r.offset_sample = 777; r.length_samples = 4800000; r.gain_db = -3.2; r.fade_in_samples = 480; r.fade_out_samples = 9600; r.muted = true;
  p.regions.push_back(r);
  domain::Region m; m.id = p.allocate_id(); m.track_id = keys; m.kind = domain::RegionKind::kMidi; m.length_samples = 96000 * 4;
  m.notes.push_back({0, 480, 0, 24000, 60, 100}); m.notes.push_back({480, 960, 24000, 48000, 67, 64});
  p.regions.push_back(m);
  p.markers.push_back({p.allocate_id(), "Chorus", 96000 * 32});

  const pal::Json j = pal::project_to_json(p);
  const std::string text = j.dump(2);
  pal::Json back;
  std::string err;
  S7_CHECK(pal::Json::parse(text, back, &err));
  domain::Project q;
  S7_CHECK(pal::project_from_json(back, q, &err));
  S7_CHECK(err.empty());
  S7_CHECK(q.name == p.name && q.sample_rate == 96000);
  S7_CHECK(q.tempo.num == 120005 && q.tempo.den == 1000);
  S7_CHECK(q.sig_num == 7 && q.sig_den == 8 && q.key == "D♭");
  S7_CHECK(q.tracks.size() == 4 && q.regions.size() == 2 && q.sources.size() == 1 && q.markers.size() == 1);
  const domain::Track* qt = q.find_track(vox);
  S7_CHECK(qt && qt->name == "Vox Ld" && qt->color == 5 && qt->output == bus && qt->mute && qt->arm);
  S7_CHECK_NEAR(qt->fader_pos, 0.6125, 0);
  S7_CHECK_NEAR(qt->pan, -0.25, 0);
  S7_CHECK(qt->inserts.size() == 5 && qt->inserts[1].name == "S7 Comp" && qt->inserts[1].bypassed && qt->inserts[1].latency_samples == 64);
  S7_CHECK(qt->sends.size() == 4 && qt->sends[1].dest_track == bus && qt->sends[1].pre && qt->sends[1].active);
  S7_CHECK_NEAR(qt->sends[1].level_db, -6.5, 0);
  S7_CHECK(q.find_track(keys)->instrument == 2 && q.find_track(keys)->kind == domain::TrackKind::kInstrument);
  const domain::Region* qr = q.find_region(r.id);
  S7_CHECK(qr && qr->start_sample == 123456789012345LL && qr->offset_sample == 777 && qr->length_samples == 4800000);
  S7_CHECK(qr->name == "vox 01 \"comp\"" && qr->muted && qr->fade_in_samples == 480 && qr->fade_out_samples == 9600);
  S7_CHECK_NEAR(qr->gain_db, -3.2, 0);
  const domain::Region* qm = q.find_region(m.id);
  S7_CHECK(qm && qm->kind == domain::RegionKind::kMidi && qm->notes.size() == 2);
  S7_CHECK(qm->notes[1].tick == 480 && qm->notes[1].length_ticks == 960 && qm->notes[1].sample == 24000 && qm->notes[1].pitch == 67 && qm->notes[1].velocity == 64);
  S7_CHECK(q.sources[0].length_samples == (1LL << 40));
  S7_CHECK(q.markers[0].name == "Chorus" && q.markers[0].sample == 96000 * 32);
  S7_CHECK(q.next_id == p.next_id);
  // Idempotent: re-serializing the loaded project is byte-identical.
  S7_CHECK(pal::project_to_json(q).dump(2) == text);
  // Newer schema is refused, not misread.
  back["schema_rev"] = pal::kSchemaRev + 1;
  S7_CHECK(!pal::project_from_json(back, q, &err));
}

}  // namespace

int main() {
  wav_round_trip_24bit_and_float();
  json_parse_and_dump();
  project_round_trip();
  return test::summary("codecs (BWF / JSON / .s7proj)");
}
