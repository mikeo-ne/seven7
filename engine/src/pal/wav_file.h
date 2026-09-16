#pragma once
// seven7 — Platform layer: WAV / BWF codec (spec: docs/01 ARC-C11 format readers; docs/03 MIX-13 export)
//
// Dependency-free reader/writer for RIFF WAVE:
//   read : PCM 8/16/24/32-bit, IEEE float 32/64, WAVE_FORMAT_EXTENSIBLE; ignores unknown chunks
//   write: 24-bit PCM or 32-bit float, with a Broadcast Wave `bext` chunk carrying the
//          origination time reference (the sample position of the take) so recordings
//          re-spot exactly (EDT-02: integer sample positions everywhere).

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine/session.h"

namespace s7::pal {

struct WavInfo {
  std::int64_t sample_rate = 0;
  int channels = 0;
  int bits = 0;
  bool is_float = false;
  std::int64_t frames = 0;
  std::int64_t time_reference = 0;  // BWF bext: sample position of first frame (0 if absent)
  std::string error;
};

namespace detail {
inline std::uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
inline std::uint16_t rd16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
inline void wr32(std::vector<unsigned char>& o, std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(static_cast<unsigned char>(v >> (8 * i))); }
inline void wr16(std::vector<unsigned char>& o, std::uint16_t v) { o.push_back(static_cast<unsigned char>(v)); o.push_back(static_cast<unsigned char>(v >> 8)); }
inline void wr_tag(std::vector<unsigned char>& o, const char* t) { o.insert(o.end(), t, t + 4); }
}  // namespace detail

/// Decode a WAV file from memory. Returns false (with info.error set) on failure.
inline bool decode_wav(const std::vector<unsigned char>& bytes, engine::SourceAudio& out, WavInfo& info) {
  using namespace detail;
  info = WavInfo{};
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    info.error = "not a RIFF/WAVE file";
    return false;
  }
  std::size_t pos = 12;
  std::uint16_t format = 0, channels = 0, bits = 0;
  std::uint32_t rate = 0;
  const unsigned char* data = nullptr;
  std::size_t data_len = 0;
  while (pos + 8 <= bytes.size()) {
    const unsigned char* c = bytes.data() + pos;
    const std::uint32_t len = rd32(c + 4);
    const std::size_t body = pos + 8;
    const std::size_t avail = bytes.size() - body;
    const std::size_t take = std::min<std::size_t>(len, avail);
    if (std::memcmp(c, "fmt ", 4) == 0 && take >= 16) {
      format = rd16(c + 8);
      channels = rd16(c + 10);
      rate = rd32(c + 12);
      bits = rd16(c + 22);
      if (format == 0xFFFE && take >= 40) format = rd16(c + 8 + 24);  // EXTENSIBLE: sub-format GUID first 2 bytes
    } else if (std::memcmp(c, "data", 4) == 0) {
      data = c + 8;
      data_len = take;
    } else if (std::memcmp(c, "bext", 4) == 0 && take >= 346) {
      // TimeReferenceLow/High at offset 338 of the bext body
      const std::uint32_t lo = rd32(c + 8 + 338), hi = rd32(c + 8 + 342);
      info.time_reference = static_cast<std::int64_t>((static_cast<std::uint64_t>(hi) << 32) | lo);
    }
    pos = body + take + (take & 1);
  }
  if (!data || channels == 0 || rate == 0 || bits == 0) { info.error = "missing fmt/data chunk"; return false; }
  const bool is_float = format == 3;
  if (!(format == 1 || format == 3)) { info.error = "unsupported WAVE format tag " + std::to_string(format); return false; }
  const int bps = bits / 8;
  if (bps < 1 || bps > 8) { info.error = "unsupported bit depth"; return false; }
  const std::size_t frame_bytes = static_cast<std::size_t>(bps) * channels;
  const std::size_t frames = data_len / frame_bytes;

  out.sample_rate = rate;
  out.ch.assign(channels, std::vector<float>(frames));
  const unsigned char* p = data;
  for (std::size_t f = 0; f < frames; ++f) {
    for (int c = 0; c < channels; ++c, p += bps) {
      float v = 0.f;
      if (is_float && bps == 4) { float t; std::memcpy(&t, p, 4); v = t; }
      else if (is_float && bps == 8) { double t; std::memcpy(&t, p, 8); v = static_cast<float>(t); }
      else if (bps == 1) v = (static_cast<int>(p[0]) - 128) / 128.f;
      else if (bps == 2) v = static_cast<std::int16_t>(rd16(p)) / 32768.f;
      else if (bps == 3) { std::int32_t t = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); v = static_cast<float>(t >> 8) / 8388608.f; }
      else if (bps == 4) v = static_cast<std::int32_t>(rd32(p)) / 2147483648.f;
      out.ch[static_cast<std::size_t>(c)][f] = v;
    }
  }
  info.sample_rate = rate;
  info.channels = channels;
  info.bits = bits;
  info.is_float = is_float;
  info.frames = static_cast<std::int64_t>(frames);
  return true;
}

inline bool read_wav(const std::string& path, engine::SourceAudio& out, WavInfo& info) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { info.error = "cannot open " + path; return false; }
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return decode_wav(bytes, out, info);
}

/// Encode as BWF. `bits` = 24 (PCM) or 32 (float). `time_reference` = timeline sample of frame 0.
inline std::vector<unsigned char> encode_wav(const engine::SourceAudio& a, int bits = 24, std::int64_t time_reference = 0,
                                             const std::string& description = "seven7") {
  using namespace detail;
  const int channels = a.channels();
  const std::int64_t frames = a.length();
  const bool is_float = bits == 32;
  const int bps = is_float ? 4 : 3;
  const std::uint32_t data_len = static_cast<std::uint32_t>(frames * channels * bps);

  std::vector<unsigned char> o;
  o.reserve(static_cast<std::size_t>(data_len) + 1024);
  wr_tag(o, "RIFF"); wr32(o, 0); wr_tag(o, "WAVE");

  // fmt
  wr_tag(o, "fmt "); wr32(o, 16);
  wr16(o, is_float ? 3 : 1); wr16(o, static_cast<std::uint16_t>(channels)); wr32(o, static_cast<std::uint32_t>(a.sample_rate));
  wr32(o, static_cast<std::uint32_t>(a.sample_rate * channels * bps)); wr16(o, static_cast<std::uint16_t>(channels * bps)); wr16(o, static_cast<std::uint16_t>(bits));

  // bext (EBU 3285 v1: 602 bytes + coding history)
  wr_tag(o, "bext"); wr32(o, 602);
  const std::size_t bext_start = o.size();
  o.resize(o.size() + 602, 0);
  unsigned char* b = o.data() + bext_start;
  std::memcpy(b, description.c_str(), std::min<std::size_t>(255, description.size()));          // Description[256]
  std::memcpy(b + 256, "seven7", 6);                                                             // Originator[32]
  std::memcpy(b + 288, "seven7", 6);                                                             // OriginatorReference[32]
  std::memcpy(b + 320, "2026-09-15", 10);                                                        // OriginationDate[10]
  std::memcpy(b + 330, "00:00:00", 8);                                                           // OriginationTime[8]
  const std::uint64_t tr = static_cast<std::uint64_t>(time_reference);
  for (int i = 0; i < 4; ++i) b[338 + i] = static_cast<unsigned char>(tr >> (8 * i));            // TimeReferenceLow
  for (int i = 0; i < 4; ++i) b[342 + i] = static_cast<unsigned char>(tr >> (32 + 8 * i));       // TimeReferenceHigh
  b[346] = 1; b[347] = 0;                                                                        // Version 1

  // data
  wr_tag(o, "data"); wr32(o, data_len);
  for (std::int64_t f = 0; f < frames; ++f) {
    for (int c = 0; c < channels; ++c) {
      const float v = a.ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(f)];
      if (is_float) { std::uint32_t u; std::memcpy(&u, &v, 4); wr32(o, u); }
      else {
        const double clamped = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        const double scaled = clamped * 8388608.0;  // symmetric with the decoder's 2^23
        const std::int32_t s = static_cast<std::int32_t>(scaled >= 8388607.0 ? 8388607.0 : (scaled < 0 ? scaled - 0.5 : scaled + 0.5));
        o.push_back(static_cast<unsigned char>(s)); o.push_back(static_cast<unsigned char>(s >> 8)); o.push_back(static_cast<unsigned char>(s >> 16));
      }
    }
  }
  if (o.size() & 1) o.push_back(0);
  const std::uint32_t riff_len = static_cast<std::uint32_t>(o.size() - 8);
  for (int i = 0; i < 4; ++i) o[4 + static_cast<std::size_t>(i)] = static_cast<unsigned char>(riff_len >> (8 * i));
  return o;
}

inline bool write_wav(const std::string& path, const engine::SourceAudio& a, int bits = 24, std::int64_t time_reference = 0) {
  const auto bytes = encode_wav(a, bits, time_reference);
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(f);
}

}  // namespace s7::pal
