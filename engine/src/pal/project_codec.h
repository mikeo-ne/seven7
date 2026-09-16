#pragma once
// seven7 — Platform layer: `.s7proj` codec (spec: docs/01 ARC-T05 / ARC-MDL)
//
// v0.1 on-disk layout (a directory bundle):
//   Name.s7proj/
//   ├── manifest.json          { app, schema_rev, created }
//   ├── project.json           the domain::Project (this file's schema)
//   └── Media/Audio Files/     BWF sources referenced by name
//
// The spec's SQLite `project.db` replaces project.json at a later schema_rev;
// the JSON form is the migration source and stays the CI-diffable format.

#include <string>

#include "domain/project.h"
#include "pal/json.h"

namespace s7::pal {

inline constexpr int kSchemaRev = 1;

inline const char* track_kind_name(domain::TrackKind k) {
  switch (k) {
    case domain::TrackKind::kAudio: return "audio";
    case domain::TrackKind::kInstrument: return "instrument";
    case domain::TrackKind::kAux: return "aux";
    case domain::TrackKind::kBus: return "bus";
    case domain::TrackKind::kVca: return "vca";
    case domain::TrackKind::kMaster: return "master";
  }
  return "audio";
}

inline domain::TrackKind track_kind_from(const std::string& s) {
  if (s == "instrument") return domain::TrackKind::kInstrument;
  if (s == "aux") return domain::TrackKind::kAux;
  if (s == "bus") return domain::TrackKind::kBus;
  if (s == "vca") return domain::TrackKind::kVca;
  if (s == "master") return domain::TrackKind::kMaster;
  return domain::TrackKind::kAudio;
}

inline Json project_to_json(const domain::Project& p) {
  Json j = Json::object();
  j["schema_rev"] = kSchemaRev;
  j["name"] = p.name;
  j["sample_rate"] = p.sample_rate;
  j["tempo"] = Json{{"num", Json(p.tempo.num)}, {"den", Json(p.tempo.den)}};
  j["sig"] = Json{{"num", Json(p.sig_num)}, {"den", Json(p.sig_den)}};
  j["key"] = p.key;
  j["next_id"] = p.next_id;

  Json tracks = Json::array();
  for (const auto& t : p.tracks) {
    Json o = Json::object();
    o["id"] = t.id; o["name"] = t.name; o["kind"] = track_kind_name(t.kind); o["color"] = t.color;
    o["mute"] = t.mute; o["solo"] = t.solo; o["arm"] = t.arm; o["monitor"] = t.monitor;
    o["fader_pos"] = t.fader_pos; o["pan"] = t.pan; o["input_channel"] = t.input_channel; o["output"] = t.output;
    o["automation_mode"] = t.automation_mode; o["instrument"] = t.instrument;
    Json ins = Json::array();
    for (const auto& i : t.inserts) ins.push(Json{{"name", Json(i.name)}, {"bypassed", Json(i.bypassed)}, {"latency", Json(i.latency_samples)}});
    o["inserts"] = ins;
    Json snd = Json::array();
    for (const auto& s : t.sends) snd.push(Json{{"dest", Json(s.dest_track)}, {"level_db", Json(s.level_db)}, {"pre", Json(s.pre)}, {"muted", Json(s.muted)}, {"active", Json(s.active)}});
    o["sends"] = snd;
    tracks.push(o);
  }
  j["tracks"] = tracks;

  Json regions = Json::array();
  for (const auto& r : p.regions) {
    Json o = Json::object();
    o["id"] = r.id; o["track"] = r.track_id; o["kind"] = r.kind == domain::RegionKind::kMidi ? "midi" : "audio";
    o["source"] = r.source_id; o["name"] = r.name; o["color"] = r.color;
    o["start"] = r.start_sample; o["offset"] = r.offset_sample; o["length"] = r.length_samples;
    o["gain_db"] = r.gain_db; o["fade_in"] = r.fade_in_samples; o["fade_out"] = r.fade_out_samples; o["muted"] = r.muted;
    if (!r.notes.empty()) {
      Json notes = Json::array();
      for (const auto& n : r.notes)
        notes.push(Json::array({Json(n.tick), Json(n.length_ticks), Json(n.sample), Json(n.length_samples), Json(static_cast<int>(n.pitch)), Json(static_cast<int>(n.velocity))}));
      o["notes"] = notes;
    }
    regions.push(o);
  }
  j["regions"] = regions;

  Json sources = Json::array();
  for (const auto& s : p.sources)
    sources.push(Json{{"id", Json(s.id)}, {"name", Json(s.name)}, {"length", Json(s.length_samples)}, {"channels", Json(s.channels)}, {"sample_rate", Json(s.sample_rate)}});
  j["sources"] = sources;

  Json markers = Json::array();
  for (const auto& m : p.markers) markers.push(Json{{"id", Json(m.id)}, {"name", Json(m.name)}, {"sample", Json(m.sample)}});
  j["markers"] = markers;
  return j;
}

inline bool project_from_json(const Json& j, domain::Project& p, std::string* error = nullptr) {
  if (!j.is_object()) { if (error) *error = "project root is not an object"; return false; }
  const int rev = static_cast<int>(j["schema_rev"].as_int(1));
  if (rev > kSchemaRev) { if (error) *error = "project schema_rev " + std::to_string(rev) + " is newer than this build"; return false; }
  p = domain::Project{};
  p.name = j["name"].as_string("Untitled");
  p.sample_rate = j["sample_rate"].as_int(48000);
  p.tempo = {j["tempo"]["num"].as_int(120), j["tempo"]["den"].as_int(1)};
  if (p.tempo.num <= 0 || p.tempo.den <= 0) p.tempo = {120, 1};
  p.sig_num = static_cast<int>(j["sig"]["num"].as_int(4));
  p.sig_den = static_cast<int>(j["sig"]["den"].as_int(4));
  p.key = j["key"].as_string("C");
  p.next_id = static_cast<domain::Id>(j["next_id"].as_int(1));

  for (const Json& o : j["tracks"].as_array()) {
    domain::Track t;
    t.id = static_cast<domain::Id>(o["id"].as_int()); t.name = o["name"].as_string(); t.kind = track_kind_from(o["kind"].as_string());
    t.color = static_cast<int>(o["color"].as_int()); t.mute = o["mute"].as_bool(); t.solo = o["solo"].as_bool();
    t.arm = o["arm"].as_bool(); t.monitor = o["monitor"].as_bool(); t.fader_pos = o["fader_pos"].as_double(0.75);
    t.pan = o["pan"].as_double(); t.input_channel = static_cast<int>(o["input_channel"].as_int()); t.output = static_cast<domain::Id>(o["output"].as_int());
    t.automation_mode = o["automation_mode"].as_string("read"); t.instrument = static_cast<int>(o["instrument"].as_int());
    for (const Json& i : o["inserts"].as_array()) { domain::Insert in; in.name = i["name"].as_string(); in.bypassed = i["bypassed"].as_bool(); in.latency_samples = i["latency"].as_int(); t.inserts.push_back(in); }
    for (const Json& s : o["sends"].as_array()) { domain::Send sn; sn.dest_track = static_cast<domain::Id>(s["dest"].as_int()); sn.level_db = s["level_db"].as_double(); sn.pre = s["pre"].as_bool(); sn.muted = s["muted"].as_bool(); sn.active = s["active"].as_bool(); t.sends.push_back(sn); }
    p.tracks.push_back(std::move(t));
  }
  for (const Json& o : j["regions"].as_array()) {
    domain::Region r;
    r.id = static_cast<domain::Id>(o["id"].as_int()); r.track_id = static_cast<domain::Id>(o["track"].as_int());
    r.kind = o["kind"].as_string() == "midi" ? domain::RegionKind::kMidi : domain::RegionKind::kAudio;
    r.source_id = static_cast<domain::Id>(o["source"].as_int()); r.name = o["name"].as_string(); r.color = static_cast<int>(o["color"].as_int(-1));
    r.start_sample = o["start"].as_int(); r.offset_sample = o["offset"].as_int(); r.length_samples = o["length"].as_int();
    r.gain_db = o["gain_db"].as_double(); r.fade_in_samples = o["fade_in"].as_int(); r.fade_out_samples = o["fade_out"].as_int(); r.muted = o["muted"].as_bool();
    for (const Json& n : o["notes"].as_array()) {
      domain::Note nt;
      nt.tick = n.at(0).as_int(); nt.length_ticks = n.at(1).as_int(); nt.sample = n.at(2).as_int(); nt.length_samples = n.at(3).as_int();
      nt.pitch = static_cast<std::uint8_t>(n.at(4).as_int(60)); nt.velocity = static_cast<std::uint8_t>(n.at(5).as_int(100));
      r.notes.push_back(nt);
    }
    p.regions.push_back(std::move(r));
  }
  for (const Json& o : j["sources"].as_array()) {
    domain::Source s;
    s.id = static_cast<domain::Id>(o["id"].as_int()); s.name = o["name"].as_string(); s.length_samples = o["length"].as_int();
    s.channels = static_cast<int>(o["channels"].as_int(1)); s.sample_rate = o["sample_rate"].as_int(48000);
    p.sources.push_back(std::move(s));
  }
  for (const Json& o : j["markers"].as_array()) {
    domain::Marker m;
    m.id = static_cast<domain::Id>(o["id"].as_int()); m.name = o["name"].as_string(); m.sample = o["sample"].as_int();
    p.markers.push_back(std::move(m));
  }
  // Ensure next_id is beyond every id (defensive against hand-edited files).
  for (const auto& t : p.tracks) p.next_id = std::max(p.next_id, t.id + 1);
  for (const auto& r : p.regions) p.next_id = std::max(p.next_id, r.id + 1);
  for (const auto& s : p.sources) p.next_id = std::max(p.next_id, s.id + 1);
  for (const auto& m : p.markers) p.next_id = std::max(p.next_id, m.id + 1);
  return true;
}

inline Json manifest_json() {
  Json m = Json::object();
  m["app"] = "seven7";
  m["schema_rev"] = kSchemaRev;
  m["format"] = "s7proj";
  return m;
}

}  // namespace s7::pal
