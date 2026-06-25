// atzcd — the relay daemon. Owns one Wine-hosted conversion engine and serves
// convert requests to fcitx5 / ibus / modore over a Unix-domain socket.
//
//   atzcd --engine-dir <path> [--socket <path>] [--type-delay-ms N]
//
// Protocol: see docs/protocol.md (one TSV line per request/reply).

#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "atzc/client.h"  // default_socket_path()
#include "atzc/proto.h"
#include "atzcd/harness_engine.h"
#include "atzcd/session.h"

namespace {

std::atomic<int> g_listen_fd{-1};
std::string g_socket_path;

void cleanup_and_exit(int sig) {
  int fd = g_listen_fd.exchange(-1);
  if (fd >= 0) ::close(fd);
  if (!g_socket_path.empty()) ::unlink(g_socket_path.c_str());
  std::_Exit(sig ? 128 + sig : 0);
}

// Read one '\n'-terminated line from fd into *line (without the newline).
// Returns false on EOF/error. Keeps leftover bytes in *buf across calls.
bool read_line(int fd, std::string *buf, std::string *line) {
  for (;;) {
    if (auto nl = buf->find('\n'); nl != std::string::npos) {
      *line = buf->substr(0, nl);
      buf->erase(0, nl + 1);
      return true;
    }
    char tmp[1024];
    ssize_t n = ::read(fd, tmp, sizeof(tmp));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    buf->append(tmp, static_cast<size_t>(n));
  }
}

bool write_all(int fd, const std::string &s) {
  size_t off = 0;
  while (off < s.size()) {
    ssize_t n = ::write(fd, s.data() + off, s.size() - off);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

// Handle one client connection: serve requests until it closes.
//
//   convert\t<romaji>              -> ok\t<top-1>            (fast; prefetches list)
//   candidates\t<romaji>[\t<max>]  -> ok\t<top-1>\t<cand>...  (full list, usually cached)
//   ping                           -> pong
void serve_conn(int cfd, atzc::Session &session) {
  std::string buf, line;
  while (read_line(cfd, &buf, &line)) {
    auto f = atzc::split_tabs(line);
    if (f.empty() || f[0].empty()) continue;

    if (f[0] == "ping") {
      if (!write_all(cfd, "pong\n")) break;
      continue;
    }
    if (f[0] == "convert" || f[0] == "candidates") {
      if (f.size() < 2) {
        write_all(cfd, atzc::join_tabs({"err", f[0] + ": missing romaji"}));
        continue;
      }
      atzc::ConvertResult r;
      std::string err;
      bool ok;
      if (f[0] == "convert") {
        ok = session.top1(f[1], &r, &err);  // fast top-1; list prefetched
      } else {
        int max = f.size() > 2 ? std::atoi(f[2].c_str()) : 0;
        ok = session.candidates(f[1], max, &r, &err);  // full list
      }
      if (!ok) {
        if (!write_all(cfd, atzc::join_tabs({"err", err}))) break;
        continue;
      }
      std::vector<std::string> reply{"ok", r.commit};
      for (auto &c : r.candidates) reply.push_back(c);
      if (!write_all(cfd, atzc::join_tabs(reply))) break;
      continue;
    }
    if (!write_all(cfd, atzc::join_tabs({"err", "unknown op: " + f[0]}))) break;
  }
  ::close(cfd);
}

[[noreturn]] void usage(const char *argv0, int code) {
  std::fprintf(code ? stderr : stdout,
               "usage: %s --engine-dir <path> [--socket <path>] [--type-delay-ms N]\n"
               "  --engine-dir    path to the engine directory (contains the launcher)\n"
               "  --socket        unix socket path (default: %s)\n"
               "  --type-delay-ms per-keystroke typing delay (0 = none)\n",
               argv0, atzc::default_socket_path().c_str());
  std::_Exit(code);
}

}  // namespace

int main(int argc, char **argv) {
  std::string engine_dir, socket_path;
  int type_delay_ms = 0;

  static const option opts[] = {
      {"engine-dir", required_argument, nullptr, 'd'},
      {"socket", required_argument, nullptr, 's'},
      {"type-delay-ms", required_argument, nullptr, 't'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0}};
  for (int c; (c = getopt_long(argc, argv, "d:s:t:h", opts, nullptr)) != -1;) {
    switch (c) {
      case 'd': engine_dir = optarg; break;
      case 's': socket_path = optarg; break;
      case 't': type_delay_ms = std::atoi(optarg); break;
      case 'h': usage(argv[0], 0);
      default: usage(argv[0], 2);
    }
  }
  if (engine_dir.empty()) {
    std::fprintf(stderr, "atzcd: --engine-dir is required\n");
    usage(argv[0], 2);
  }
  if (socket_path.empty()) socket_path = atzc::default_socket_path();
  g_socket_path = socket_path;

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, cleanup_and_exit);
  signal(SIGTERM, cleanup_and_exit);

  // Bring the engine up first; no point opening the socket if it fails.
  atzc::HarnessEngine engine(engine_dir, type_delay_ms);
  std::fprintf(stderr, "atzcd: starting engine (cold bring-up can take minutes)...\n");
  if (std::string err; !engine.start(&err)) {
    std::fprintf(stderr, "atzcd: engine failed to start: %s\n", err.c_str());
    return 1;
  }
  std::fprintf(stderr, "atzcd: engine ready\n");
  atzc::Session session(&engine);

  int lfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (lfd < 0) {
    std::perror("atzcd: socket");
    return 1;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socket_path.size() >= sizeof(addr.sun_path)) {
    std::fprintf(stderr, "atzcd: socket path too long\n");
    return 1;
  }
  std::strcpy(addr.sun_path, socket_path.c_str());
  ::unlink(socket_path.c_str());
  if (::bind(lfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    std::perror("atzcd: bind");
    return 1;
  }
  if (::listen(lfd, 8) != 0) {
    std::perror("atzcd: listen");
    return 1;
  }
  g_listen_fd = lfd;
  std::fprintf(stderr, "atzcd: listening on %s\n", socket_path.c_str());

  // One connection at a time: the engine is a singleton and serial anyway.
  for (;;) {
    int cfd = ::accept(lfd, nullptr, nullptr);
    if (cfd < 0) {
      if (errno == EINTR) continue;
      std::perror("atzcd: accept");
      break;
    }
    serve_conn(cfd, session);
  }

  cleanup_and_exit(0);
}
