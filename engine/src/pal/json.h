#pragma once
// seven7 — Platform layer: minimal JSON value + parser/serializer
//
// Used for `.s7proj` project files (ARC-T05: manifest + model) and for the UI
// bridge messages. Dependency-free; ~250 lines; strict enough for our own output
// and tolerant of whitespace/ordering in hand-edited files.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace s7::pal {

class Json {
public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool b) : type_{Type::kBool}, b_{b} {}
  Json(int v) : type_{Type::kNumber}, n_{static_cast<double>(v)} {}
  Json(std::int64_t v) : type_{Type::kNumber}, n_{static_cast<double>(v)} { i_ = v; exact_ = true; }
  Json(std::uint32_t v) : Json(static_cast<std::int64_t>(v)) {}
  Json(double v) : type_{Type::kNumber}, n_{v} {}
  Json(const char* s) : type_{Type::kString}, s_{s} {}
  Json(std::string s) : type_{Type::kString}, s_{std::move(s)} {}
  // Array/Object take explicit tags: `Json(Array)` alone would be recursive because a
  // std::vector<Json> is itself constructible from a Json (initializer-list ctor).
  struct ArrayTag {};
  struct ObjectTag {};
  Json(ArrayTag, Array a) : type_{Type::kArray}, a_{std::move(a)} {}
  Json(ObjectTag, Object o) : type_{Type::kObject}, o_{std::move(o)} {}
  Json(std::initializer_list<std::pair<const std::string, Json>> kv) : type_{Type::kObject}, o_{kv} {}

  static Json object() { return Json(ObjectTag{}, Object{}); }
  static Json array() { return Json(ArrayTag{}, Array{}); }
  static Json array(Array a) { return Json(ArrayTag{}, std::move(a)); }
  static Json object(Object o) { return Json(ObjectTag{}, std::move(o)); }

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::kNull; }
  bool is_object() const { return type_ == Type::kObject; }
  bool is_array() const { return type_ == Type::kArray; }
  bool is_number() const { return type_ == Type::kNumber; }
  bool is_string() const { return type_ == Type::kString; }
  bool is_bool() const { return type_ == Type::kBool; }

  bool as_bool(bool def = false) const { return type_ == Type::kBool ? b_ : (type_ == Type::kNumber ? n_ != 0 : def); }
  double as_double(double def = 0) const { return type_ == Type::kNumber ? n_ : def; }
  std::int64_t as_int(std::int64_t def = 0) const {
    if (type_ != Type::kNumber) return def;
    return exact_ ? i_ : static_cast<std::int64_t>(std::llround(n_));
  }
  const std::string& as_string() const { static const std::string empty; return type_ == Type::kString ? s_ : empty; }
  std::string as_string(const std::string& def) const { return type_ == Type::kString ? s_ : def; }
  const Array& as_array() const { static const Array empty; return type_ == Type::kArray ? a_ : empty; }
  const Object& as_object() const { static const Object empty; return type_ == Type::kObject ? o_ : empty; }

  // object access
  const Json& operator[](const std::string& key) const {
    static const Json null;
    if (type_ != Type::kObject) return null;
    auto it = o_.find(key);
    return it == o_.end() ? null : it->second;
  }
  Json& operator[](const std::string& key) {
    if (type_ != Type::kObject) { *this = object(); }
    return o_[key];
  }
  bool has(const std::string& key) const { return type_ == Type::kObject && o_.count(key) != 0; }
  void set(const std::string& key, Json v) { (*this)[key] = std::move(v); }

  // array access
  const Json& at(std::size_t i) const { static const Json null; return type_ == Type::kArray && i < a_.size() ? a_[i] : null; }
  void push(Json v) { if (type_ != Type::kArray) *this = array(); a_.push_back(std::move(v)); }
  std::size_t size() const { return type_ == Type::kArray ? a_.size() : (type_ == Type::kObject ? o_.size() : 0); }

  // ── serialize ──────────────────────────────────────────────────────────────
  std::string dump(int indent = 0) const { std::string out; write(out, indent, 0); return out; }

  // ── parse ──────────────────────────────────────────────────────────────────
  static bool parse(const std::string& text, Json& out, std::string* error = nullptr) {
    Parser p{text, 0, error};
    p.ws();
    if (!p.value(out)) return false;
    p.ws();
    if (p.i != text.size()) { p.fail("trailing characters"); return false; }
    return true;
  }

private:
  void write(std::string& o, int indent, int depth) const {
    switch (type_) {
      case Type::kNull: o += "null"; break;
      case Type::kBool: o += b_ ? "true" : "false"; break;
      case Type::kNumber: {
        if (exact_) { o += std::to_string(i_); break; }
        if (std::isfinite(n_) && n_ == std::floor(n_) && std::fabs(n_) < 1e15) { o += std::to_string(static_cast<long long>(n_)); break; }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.17g", std::isfinite(n_) ? n_ : 0.0);
        o += buf;
        break;
      }
      case Type::kString: write_string(o, s_); break;
      case Type::kArray: {
        if (a_.empty()) { o += "[]"; break; }
        o += '[';
        for (std::size_t i = 0; i < a_.size(); ++i) {
          if (i) o += ',';
          newline(o, indent, depth + 1);
          a_[i].write(o, indent, depth + 1);
        }
        newline(o, indent, depth);
        o += ']';
        break;
      }
      case Type::kObject: {
        if (o_.empty()) { o += "{}"; break; }
        o += '{';
        bool first = true;
        for (const auto& [k, v] : o_) {
          if (!first) o += ',';
          first = false;
          newline(o, indent, depth + 1);
          write_string(o, k);
          o += indent ? ": " : ":";
          v.write(o, indent, depth + 1);
        }
        newline(o, indent, depth);
        o += '}';
        break;
      }
    }
  }
  static void newline(std::string& o, int indent, int depth) {
    if (!indent) return;
    o += '\n';
    o.append(static_cast<std::size_t>(indent * depth), ' ');
  }
  static void write_string(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
      switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
          if (c < 0x20) { char buf[8]; std::snprintf(buf, sizeof buf, "\\u%04x", c); o += buf; }
          else o += static_cast<char>(c);
      }
    }
    o += '"';
  }

  struct Parser {
    const std::string& t;
    std::size_t i;
    std::string* err;
    void fail(const char* m) { if (err && err->empty()) *err = std::string(m) + " at offset " + std::to_string(i); }
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i; }
    bool value(Json& out) {
      if (i >= t.size()) { fail("unexpected end"); return false; }
      const char c = t[i];
      if (c == '{') return object(out);
      if (c == '[') return array(out);
      if (c == '"') { std::string s; if (!string(s)) return false; out = Json(std::move(s)); return true; }
      if (c == 't' && t.compare(i, 4, "true") == 0) { i += 4; out = Json(true); return true; }
      if (c == 'f' && t.compare(i, 5, "false") == 0) { i += 5; out = Json(false); return true; }
      if (c == 'n' && t.compare(i, 4, "null") == 0) { i += 4; out = Json(); return true; }
      if (c == '-' || (c >= '0' && c <= '9')) return number(out);
      fail("unexpected character");
      return false;
    }
    bool number(Json& out) {
      const std::size_t start = i;
      bool integral = true;
      if (t[i] == '-') ++i;
      while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
      if (i < t.size() && t[i] == '.') { integral = false; ++i; while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i; }
      if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) { integral = false; ++i; if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i; while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i; }
      const std::string s = t.substr(start, i - start);
      if (integral && s.size() < 19) out = Json(static_cast<std::int64_t>(std::stoll(s)));
      else out = Json(std::stod(s));
      return true;
    }
    bool string(std::string& s) {
      ++i;  // opening quote
      while (i < t.size()) {
        const char c = t[i++];
        if (c == '"') return true;
        if (c == '\\') {
          if (i >= t.size()) break;
          const char e = t[i++];
          switch (e) {
            case '"': s += '"'; break; case '\\': s += '\\'; break; case '/': s += '/'; break;
            case 'b': s += '\b'; break; case 'f': s += '\f'; break; case 'n': s += '\n'; break;
            case 'r': s += '\r'; break; case 't': s += '\t'; break;
            case 'u': {
              if (i + 4 > t.size()) { fail("bad \\u escape"); return false; }
              unsigned cp = static_cast<unsigned>(std::stoul(t.substr(i, 4), nullptr, 16));
              i += 4;
              if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                const unsigned lo = static_cast<unsigned>(std::stoul(t.substr(i + 2, 4), nullptr, 16));
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i += 6;
              }
              if (cp < 0x80) s += static_cast<char>(cp);
              else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
              else if (cp < 0x10000) { s += static_cast<char>(0xE0 | (cp >> 12)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
              else { s += static_cast<char>(0xF0 | (cp >> 18)); s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
              break;
            }
            default: fail("bad escape"); return false;
          }
        } else s += c;
      }
      fail("unterminated string");
      return false;
    }
    bool array(Json& out) {
      ++i;
      out = Json::array();
      ws();
      if (i < t.size() && t[i] == ']') { ++i; return true; }
      while (true) {
        Json v;
        ws();
        if (!value(v)) return false;
        out.push(std::move(v));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == ']') { ++i; return true; }
        fail("expected , or ]");
        return false;
      }
    }
    bool object(Json& out) {
      ++i;
      out = Json::object();
      ws();
      if (i < t.size() && t[i] == '}') { ++i; return true; }
      while (true) {
        ws();
        if (i >= t.size() || t[i] != '"') { fail("expected key"); return false; }
        std::string k;
        if (!string(k)) return false;
        ws();
        if (i >= t.size() || t[i] != ':') { fail("expected :"); return false; }
        ++i;
        ws();
        Json v;
        if (!value(v)) return false;
        out.set(k, std::move(v));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == '}') { ++i; return true; }
        fail("expected , or }");
        return false;
      }
    }
  };

  Type type_ = Type::kNull;
  bool b_ = false;
  double n_ = 0;
  std::int64_t i_ = 0;
  bool exact_ = false;
  std::string s_;
  Array a_;
  Object o_;
};

}  // namespace s7::pal
