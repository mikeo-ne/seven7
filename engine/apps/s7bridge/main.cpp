// seven7 — s7bridge: development bridge (docs/07-ui-bridge-protocol.md §4)
//
// Serves the Controller over plain HTTP so the React UI can run in a browser
// during development, driving the *real* C++ engine:
//
//   POST /api/command        body: JSON command      → JSON reply
//   GET  /api/state          → full project state
//   GET  /api/status         → transport + meters (~poll at 30 Hz)
//   GET  /api/peaks?source=&from=&to=&buckets=
//   GET  /api/render?start=&end=   → 16-bit WAV of an offline bounce (browser preview audio)
//
// In the desktop app the same Controller sits behind JUCE's WebBrowserComponent
// native functions (apps/seven7/), so the UI code path is identical. This binary
// has no audio device: playback is simulated by a clock thread that pumps the
// Session at real-time rate so transport, meters and recording logic behave
// exactly as on hardware. Never ship this; it is a developer tool.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/controller.h"
#include "pal/wav_file.h"

namespace {

std::mutex g_mutex;  // the Controller is single-threaded (message thread); serialize HTTP handlers

struct Request {
  std::string method, path, query, body;
  std::map<std::string, std::string> params;
};

std::string url_decode(const std::string& s) {
  std::string o;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) { o += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16)); i += 2; }
    else if (s[i] == '+') o += ' ';
    else o += s[i];
  }
  return o;
}

bool read_request(int fd, Request& r) {
  std::string data;
  char buf[8192];
  std::size_t header_end = std::string::npos;
  while (header_end == std::string::npos) {
    const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n <= 0) return false;
    data.append(buf, static_cast<std::size_t>(n));
    header_end = data.find("\r\n\r\n");
    if (data.size() > (1u << 20)) return false;
  }
  std::istringstream head(data.substr(0, header_end));
  std::string line;
  std::getline(head, line);
  std::istringstream first(line);
  std::string target;
  first >> r.method >> target;
  std::size_t content_length = 0;
  while (std::getline(head, line)) {
    if (line.rfind("Content-Length:", 0) == 0 || line.rfind("content-length:", 0) == 0) content_length = static_cast<std::size_t>(std::stoul(line.substr(15)));
  }
  r.body = data.substr(header_end + 4);
  while (r.body.size() < content_length) {
    const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n <= 0) return false;
    r.body.append(buf, static_cast<std::size_t>(n));
  }
  const std::size_t q = target.find('?');
  r.path = target.substr(0, q);
  if (q != std::string::npos) {
    r.query = target.substr(q + 1);
    std::istringstream qs(r.query);
    std::string kv;
    while (std::getline(qs, kv, '&')) {
      const std::size_t eq = kv.find('=');
      r.params[url_decode(kv.substr(0, eq))] = eq == std::string::npos ? "" : url_decode(kv.substr(eq + 1));
    }
  }
  return true;
}

void respond(int fd, int code, const std::string& mime, const std::string& body) {
  std::string h = "HTTP/1.1 " + std::to_string(code) + (code == 200 ? " OK" : code == 404 ? " Not Found" : " Error") + "\r\n";
  h += "Content-Type: " + mime + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
  h += "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
  h += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
  const std::string out = h + body;
  std::size_t sent = 0;
  while (sent < out.size()) {
    const ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
    if (n <= 0) return;
    sent += static_cast<std::size_t>(n);
  }
}

std::int64_t param_int(const Request& r, const char* k, std::int64_t def) {
  auto it = r.params.find(k);
  return it == r.params.end() || it->second.empty() ? def : std::stoll(it->second);
}

}  // namespace

int main(int argc, char** argv) {
  int port = 8787;
  std::string open_path;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--port") == 0) port = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--open") == 0) open_path = argv[i + 1];
  }

  s7::app::Controller ctl;
  ctl.on_log = [](const std::string& m) { std::printf("[s7] %s\n", m.c_str()); std::fflush(stdout); };
  ctl.prepare(48000, 256);
  if (!open_path.empty()) { std::string err; if (!ctl.load_bundle(open_path, &err)) std::printf("[s7] open failed: %s\n", err.c_str()); }

  // Clock thread: pumps the Session at real-time rate (256 frames per 5.33 ms) so the
  // transport advances, meters move and recording works without an audio device.
  std::atomic<bool> running{true};
  std::thread clock([&] {
    std::vector<float> l(256), r(256);
    float* outs[2] = {l.data(), r.data()};
    auto next = std::chrono::steady_clock::now();
    while (running.load()) {
      { std::lock_guard<std::mutex> lock(g_mutex); ctl.session().process(nullptr, 0, outs, 2, 256); }
      next += std::chrono::microseconds(256 * 1000000 / 48000);
      std::this_thread::sleep_until(next);
    }
  });
  std::thread service([&] {
    while (running.load()) {
      { std::lock_guard<std::mutex> lock(g_mutex); ctl.tick(); }
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
  });

  const int server = ::socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(server, 64) != 0) {
    std::perror("s7bridge: bind");
    running.store(false);
    clock.join();
    service.join();
    return 1;
  }
  std::printf("s7bridge listening on http://0.0.0.0:%d  (POST /api/command · GET /api/state · GET /api/status)\n", port);
  std::fflush(stdout);

  while (true) {
    const int fd = ::accept(server, nullptr, nullptr);
    if (fd < 0) continue;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    Request req;
    if (!read_request(fd, req)) { ::close(fd); continue; }
    if (req.method == "OPTIONS") { respond(fd, 200, "text/plain", ""); ::close(fd); continue; }

    std::string body, mime = "application/json";
    int code = 200;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (req.path == "/api/command" && req.method == "POST") body = ctl.command_text(req.body).dump();
      else if (req.path == "/api/state") body = ctl.state_json().dump();
      else if (req.path == "/api/status") body = ctl.status_json().dump();
      else if (req.path == "/api/peaks") body = ctl.peaks_json(static_cast<s7::domain::Id>(param_int(req, "source", 0)), param_int(req, "from", 0), param_int(req, "to", 0), static_cast<int>(param_int(req, "buckets", 512))).dump();
      else if (req.path == "/api/render") {
        // Offline bounce of [start,end) for in-browser audition (the browser has no access to the engine's DAC).
        const std::int64_t start = param_int(req, "start", 0);
        const std::int64_t end = std::min(param_int(req, "end", ctl.project().content_end_sample()), start + ctl.project().sample_rate * 600);
        auto snap = s7::engine::compile_snapshot(ctl.project(), ctl.sources(), s7::engine::SchedulerConfig{ctl.project().sample_rate, 256, 4096, 32768});
        auto mix = s7::engine::render_offline(std::move(snap), start, std::max(end, start + 1), 512);
        s7::engine::SourceAudio a;
        a.sample_rate = ctl.project().sample_rate;
        a.ch = std::move(mix);
        const auto bytes = s7::pal::encode_wav(a, 32, start, "seven7 preview");
        body.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        mime = "audio/wav";
      } else if (req.path == "/api/health") body = "{\"ok\":true,\"app\":\"seven7\",\"bridge\":\"s7bridge\"}";
      else { code = 404; body = "{\"ok\":false,\"error\":\"not found\"}"; }
    }
    respond(fd, code, mime, body);
    ::close(fd);
  }
}
