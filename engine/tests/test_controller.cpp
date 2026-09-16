// seven7 — controller tests: the JSON UI protocol end-to-end (docs/07-ui-bridge-protocol.md)

#include <cstdint>
#include <filesystem>
#include <string>

#include "app/controller.h"
#include "pal/project_codec.h"
#include "pal/wav_file.h"
#include "s7_test.h"

using namespace s7;
using pal::Json;

namespace {

struct Ctl {
  app::Controller c;
  Ctl() { c.prepare(48000, 256); }
};

domain::Id first_track_of_kind(const app::Controller& c, domain::TrackKind k) {
  for (const auto& t : c.project().tracks) if (t.kind == k) return t.id;
  return domain::kNoId;
}

void demo_project_and_state() {
  Ctl ctl;
  app::Controller& c = ctl.c;
  const Json s = c.state_json();
  S7_CHECK(s["name"].as_string() == "Midnight City");
  S7_CHECK(s["tracks"].size() == 8);                    // 4 inst + vox + bus + aux + master
  S7_CHECK(s["regions"].size() >= 6);
  S7_CHECK(s["sources"].size() == 1);                   // synthesized 'Vox Ld Comp' take
  S7_CHECK(s["instrument_presets"].size() == 6);
  S7_CHECK(s["tracks"].at(7)["kind"].as_string() == "master");
  S7_CHECK(s["content_end"].as_int() == 8 * 96000);      // 8 bars @120 = 768000 samples
  const Json st = c.status_json();
  S7_CHECK(!st["playing"].as_bool());
  S7_CHECK(st["meters"].size() == 8);
}

void transport_and_render() {
  Ctl ctl;
  app::Controller& c = ctl.c;
  S7_CHECK(c.command_text(R"({"op":"play"})")["ok"].as_bool());
  std::vector<float> l(256), r(256);
  float* outs[2] = {l.data(), r.data()};
  float energy = 0.f;
  for (int i = 0; i < 100; ++i) { c.session().process(nullptr, 0, outs, 2, 256); for (float v : l) energy += std::fabs(v); }
  S7_CHECK(energy > 1.f);                                // the demo makes sound
  S7_CHECK(c.status_json()["playhead"].as_int() == 100 * 256);
  S7_CHECK(c.command_text(R"({"op":"locate","sample":12345})")["ok"].as_bool());
  c.session().process(nullptr, 0, outs, 2, 256);
  S7_CHECK(c.status_json()["playhead"].as_int() == 12345 + 256);
  c.command_text(R"({"op":"stop"})");
  c.session().process(nullptr, 0, outs, 2, 256);
  S7_CHECK(!c.status_json()["playing"].as_bool());
  S7_CHECK(!c.command_text(R"({"op":"bogus"})")["ok"].as_bool());
  S7_CHECK(!c.command_text("not json")["ok"].as_bool());
}

void editing_through_protocol_with_undo() {
  Ctl ctl;
  app::Controller& c = ctl.c;
  const domain::Id vox = first_track_of_kind(c, domain::TrackKind::kAudio);
  Json add = c.command(Json{{"op", Json("add_midi_region")}, {"track", Json(first_track_of_kind(c, domain::TrackKind::kInstrument))}, {"start", Json(std::int64_t{96000})}, {"length", Json(std::int64_t{48000})}});
  S7_CHECK(add["ok"].as_bool());
  const domain::Id rid = static_cast<domain::Id>(add["region"].as_int());
  const std::size_t n0 = c.project().regions.size();
  Json sp = c.command(Json{{"op", Json("split")}, {"region", Json(rid)}, {"sample", Json(std::int64_t{96000 + 1000})}});
  S7_CHECK(sp["ok"].as_bool());
  S7_CHECK(c.project().regions.size() == n0 + 1);
  const domain::Id right = static_cast<domain::Id>(sp["region"].as_int());
  S7_CHECK(c.project().find_region(right)->start_sample == 97000);
  S7_CHECK(c.project().find_region(rid)->fade_out_samples == 480);  // auto micro-fade
  S7_CHECK(c.command(Json{{"op", Json("nudge")}, {"regions", Json::array({Json(right)})}, {"delta", Json(std::int64_t{-1})}})["ok"].as_bool());
  S7_CHECK(c.project().find_region(right)->start_sample == 96999);
  S7_CHECK(c.command_text(R"({"op":"undo"})")["ok"].as_bool());
  S7_CHECK(c.project().find_region(right)->start_sample == 97000);
  S7_CHECK(c.command_text(R"({"op":"undo"})")["ok"].as_bool());
  S7_CHECK(c.project().find_region(right) == nullptr);
  S7_CHECK(c.command_text(R"({"op":"redo"})")["ok"].as_bool());
  S7_CHECK(c.project().find_region(right) != nullptr);
  S7_CHECK(c.state_json()["undo_label"].as_string() == "Split");
  // notes
  Json notes = Json::array();
  notes.push(Json{{"sample", Json(std::int64_t{0})}, {"length", Json(std::int64_t{12000})}, {"pitch", Json(64)}, {"velocity", Json(90)}});
  notes.push(Json{{"sample", Json(std::int64_t{24000})}, {"length", Json(std::int64_t{12000})}, {"pitch", Json(67)}});
  S7_CHECK(c.command(Json{{"op", Json("set_notes")}, {"region", Json(right)}, {"notes", notes}})["ok"].as_bool());
  const domain::Region* rr = c.project().find_region(right);
  S7_CHECK(rr->notes.size() == 2 && rr->notes[1].pitch == 67 && rr->notes[1].tick == 960);  // 24000 smp = 1 beat = 960 ticks
  // snap
  Json sn = c.command_text(R"({"op":"snap","unit":"beat","sample":25000})");
  S7_CHECK(sn["sample"].as_int() == 24000 && sn["step"].as_int() == 24000);
  // mixer
  S7_CHECK(c.command(Json{{"op", Json("set_track")}, {"track", Json(vox)}, {"param", Json("fader")}, {"value", Json(0.5)}})["ok"].as_bool());
  S7_CHECK(c.project().find_track(vox)->fader_pos == 0.5);
  S7_CHECK(c.command(Json{{"op", Json("set_track")}, {"track", Json(vox)}, {"param", Json("nope")}, {"value", Json(0.5)}})["ok"].as_bool() == false);
  // switches accept JSON booleans (what the UI sends) as well as numbers
  S7_CHECK(c.command_text("{\"op\":\"set_track\",\"track\":" + std::to_string(vox) + ",\"param\":\"mute\",\"value\":true}")["ok"].as_bool());
  S7_CHECK(c.project().find_track(vox)->mute);
  S7_CHECK(c.command_text("{\"op\":\"set_track\",\"track\":" + std::to_string(vox) + ",\"param\":\"mute\",\"value\":false}")["ok"].as_bool());
  S7_CHECK(!c.project().find_track(vox)->mute);
  S7_CHECK(c.command_text("{\"op\":\"set_track\",\"track\":" + std::to_string(vox) + ",\"param\":\"arm\",\"value\":true}")["ok"].as_bool());
  S7_CHECK(c.project().find_track(vox)->arm);
  c.command_text("{\"op\":\"set_track\",\"track\":" + std::to_string(vox) + ",\"param\":\"arm\",\"value\":0}");
  S7_CHECK(!c.project().find_track(vox)->arm);
  // tracks
  const std::size_t t0 = c.project().tracks.size();
  Json at = c.command_text(R"({"op":"add_track","kind":"instrument","name":"Lead","instrument":4})");
  S7_CHECK(at["ok"].as_bool() && c.project().tracks.size() == t0 + 1);
  S7_CHECK(c.project().tracks.back().kind == domain::TrackKind::kMaster);   // master stays last
  S7_CHECK(c.command(Json{{"op", Json("remove_track")}, {"track", Json(static_cast<domain::Id>(at["track"].as_int()))}})["ok"].as_bool());
  S7_CHECK(c.project().tracks.size() == t0);
  S7_CHECK(!c.command(Json{{"op", Json("remove_track")}, {"track", Json(c.project().master()->id)}})["ok"].as_bool());
}

void save_load_bundle_and_import() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "s7_ctrl_test.s7proj";
  std::error_code ec;
  fs::remove_all(dir, ec);
  Ctl ctl;
  app::Controller& c = ctl.c;
  // Bounce a second of the demo to a WAV and import it back as audio.
  auto snap = engine::compile_snapshot(c.project(), c.sources(), engine::SchedulerConfig{48000, 256, 4096, 32768});
  auto mix = engine::render_offline(std::move(snap), 0, 48000, 256);
  engine::SourceAudio a;
  a.sample_rate = 48000;
  a.ch = std::move(mix);
  const fs::path wav = fs::temp_directory_path() / "s7_ctrl_import.wav";
  S7_CHECK(pal::write_wav(wav.string(), a, 24, 0));
  const domain::Id vox = first_track_of_kind(c, domain::TrackKind::kAudio);
  S7_CHECK(c.command(Json{{"op", Json("import_audio")}, {"path", Json(wav.string())}, {"track", Json(vox)}, {"sample", Json(std::int64_t{96000})}})["ok"].as_bool());
  S7_CHECK(c.project().sources.size() == 2);
  S7_CHECK(c.sources().size() == 2);
  const domain::Region* imported = nullptr;
  for (const auto& r : c.project().regions) if (r.track_id == vox && r.start_sample == 96000) imported = &r;
  S7_CHECK(imported && imported->start_sample == 96000 && imported->length_samples == 48000);
  // peaks
  Json pk = c.command(Json{{"op", Json("get_peaks")}, {"source", Json(imported->source_id)}, {"from", Json(std::int64_t{0})}, {"to", Json(std::int64_t{48000})}, {"buckets", Json(64)}});
  S7_CHECK(pk["peaks"].size() == 128);
  bool nonzero = false;
  for (const Json& v : pk["peaks"].as_array()) if (v.as_double() != 0.0) nonzero = true;
  S7_CHECK(nonzero);

  std::string err;
  S7_CHECK(c.save_bundle(dir.string(), &err));
  S7_CHECK(fs::exists(dir / "project.json") && fs::exists(dir / "manifest.json"));
  S7_CHECK(fs::exists(dir / "Media" / "Audio Files" / "s7_ctrl_import.wav"));
  S7_CHECK(c.project().name == "s7_ctrl_test");

  Ctl ctl2;
  app::Controller& d = ctl2.c;
  S7_CHECK(d.load_bundle(dir.string(), &err));
  S7_CHECK(err.empty());
  S7_CHECK(d.project().tracks.size() == c.project().tracks.size());
  S7_CHECK(d.project().regions.size() == c.project().regions.size());
  S7_CHECK(d.sources().size() == 2);
  S7_CHECK(fs::exists(dir / "Media" / "Audio Files" / "Vox Ld Comp.wav"));
  S7_CHECK(pal::project_to_json(d.project()).dump() == pal::project_to_json(c.project()).dump());
  // and it renders identically
  auto s1 = engine::render_offline(engine::compile_snapshot(c.project(), c.sources(), engine::SchedulerConfig{}), 96000, 96000 + 4800, 256);
  auto s2 = engine::render_offline(engine::compile_snapshot(d.project(), d.sources(), engine::SchedulerConfig{}), 96000, 96000 + 4800, 256);
  double diff = 0;
  for (std::size_t i = 0; i < s1[0].size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s1[0][i] - s2[0][i])));
  S7_CHECK(diff < 1e-6);   // 24-bit media round trip
  S7_CHECK(!d.load_bundle((fs::temp_directory_path() / "does_not_exist.s7proj").string(), &err));
  fs::remove_all(dir, ec);
  fs::remove(wav, ec);
}

void recording_lands_in_project() {
  Ctl ctl;
  app::Controller& c = ctl.c;
  const domain::Id vox = first_track_of_kind(c, domain::TrackKind::kAudio);
  c.command(Json{{"op", Json("set_track")}, {"track", Json(vox)}, {"param", Json("arm")}, {"value", Json(1)}});
  c.command_text(R"({"op":"locate","sample":48000})");
  c.command_text(R"({"op":"record","on":true})");
  std::vector<float> in(256, 0.25f), l(256), r(256);
  const float* ins[1] = {in.data()};
  float* outs[2] = {l.data(), r.data()};
  for (int i = 0; i < 10; ++i) c.session().process(ins, 1, outs, 2, 256);
  c.command_text(R"({"op":"stop"})");
  c.session().process(nullptr, 0, outs, 2, 256);
  const std::size_t before = c.project().regions.size();
  S7_CHECK(c.tick());
  S7_CHECK(c.project().regions.size() == before + 1);
  const domain::Region& take = c.project().regions.back();
  S7_CHECK(take.track_id == vox && take.start_sample == 48000 && take.length_samples == 2560);
  S7_CHECK(take.fade_in_samples == 480);
  S7_CHECK(c.project().sources.back().name == "Vox Ld take 1.wav");
  S7_CHECK(c.state_json()["undo_label"].as_string() == "Record");
}

}  // namespace

int main() {
  demo_project_and_state();
  transport_and_render();
  editing_through_protocol_with_undo();
  save_load_bundle_and_import();
  recording_lands_in_project();
  return test::summary("controller (UI protocol / bundle I/O / record)");
}
