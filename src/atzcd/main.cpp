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
#include "atzc/harness_engine.h"
#include "atzc/proto.h"
#include "atzc/session.h"
#include "atzc/session_engine.h"

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

// Reply "ok" plus one optional payload field (empty payload -> bare "ok").
bool reply_ok1(int cfd, const std::string &payload) {
  return write_all(cfd, payload.empty()
                            ? std::string("ok\n")
                            : atzc::join_tabs({"ok", payload}));
}

// Handle one client connection: serve requests until it closes. Two protocol
// families share the connection (see docs/protocol.md):
//
//   BATCH (stateless, whole-romaji per request; candidate list client-side):
//     convert\t<romaji>              -> ok\t<top-1>            (fast; prefetches list)
//     candidates\t<romaji>[\t<max>]  -> ok\t<top-1>\t<cand>...  (flat surfaces)
//     candidates-ex\t<romaji>[\t<max>] -> okx\t<top-1>\t<surf>\t<reading>\t<hinsi>...
//                                       (enriched: reading + 品詞 per candidate)
//     ping                           -> pong
//
//   SESSION (stateful, one long-lived composition on the keepalive harness):
//     session-begin                  -> ok                      (start/reset a comp)
//     key\t<ascii>                   -> ok\t<kana>               (append romaji)
//     backspace                      -> ok\t<kana>               (delete one kana)
//     convert-session                -> ok\t<kanji>              (henkan in place)
//     preedit                        -> ok\t<text>               (read the preedit)
//     commit                         -> commit\t<text>           (finalize)
//     cancel                         -> ok                       (abandon)
//     session-candidates\t<romaji>[\t<max>] -> ok\t<top-1>\t<cand>...  (heavy list;
//                                       served by the BATCH single-shot mechanism)
void serve_conn(int cfd, atzc::Session &session, atzc::SessionEngine &sess) {
  std::string buf, line;
  while (read_line(cfd, &buf, &line)) {
    auto f = atzc::split_tabs(line);
    if (f.empty() || f[0].empty()) continue;
    const std::string &op = f[0];

    if (op == "ping") {
      if (!write_all(cfd, "pong\n")) break;
      continue;
    }

    // --- batch path (unchanged) ---
    if (op == "convert" || op == "candidates") {
      if (f.size() < 2) {
        write_all(cfd, atzc::join_tabs({"err", op + ": missing romaji"}));
        continue;
      }
      atzc::ConvertResult r;
      std::string err;
      bool ok;
      if (op == "convert") {
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

    // Enriched batch list: prefer candidatesEx (surface/reading/hinsi triples);
    // if the engine gave none, fall back to the flat surfaces (empty reading/
    // hinsi) so a caller always gets a usable list.
    if (op == "candidates-ex") {
      if (f.size() < 2) {
        write_all(cfd, atzc::join_tabs({"err", "candidates-ex: missing romaji"}));
        continue;
      }
      atzc::ConvertResult r;
      std::string err;
      int max = f.size() > 2 ? std::atoi(f[2].c_str()) : 0;
      if (!session.candidates(f[1], max, &r, &err)) {
        if (!write_all(cfd, atzc::join_tabs({"err", err}))) break;
        continue;
      }
      std::vector<std::string> reply{"okx", r.commit};
      if (!r.candidatesEx.empty()) {
        for (const auto &c : r.candidatesEx) {
          reply.push_back(c.surface);
          reply.push_back(c.reading);
          reply.push_back(c.hinsi);
        }
      } else {
        for (const auto &c : r.candidates) {  // flat fallback: empty reading/hinsi
          reply.push_back(c);
          reply.push_back("");
          reply.push_back("");
        }
      }
      if (!write_all(cfd, atzc::join_tabs(reply))) break;
      continue;
    }

    // --- session path: stateful composition on the keepalive harness ---
    // The heavy full-candidate-list stays on the batch single-shot mechanism.
    if (op == "session-candidates") {
      if (f.size() < 2) {
        write_all(cfd, atzc::join_tabs({"err", "session-candidates: missing romaji"}));
        continue;
      }
      atzc::ConvertResult r;
      std::string err;
      int max = f.size() > 2 ? std::atoi(f[2].c_str()) : 0;
      if (!session.candidates(f[1], max, &r, &err)) {
        if (!write_all(cfd, atzc::join_tabs({"err", err}))) break;
        continue;
      }
      std::vector<std::string> reply{"ok", r.commit};
      for (auto &c : r.candidates) reply.push_back(c);
      if (!write_all(cfd, atzc::join_tabs(reply))) break;
      continue;
    }

    std::string err, text;
    bool ok = false, handled = true;
    if (op == "session-begin") {
      ok = sess.begin(&text, &err);
    } else if (op == "key") {
      if (f.size() < 2) {
        if (!write_all(cfd, atzc::join_tabs({"err", "key: missing chars"}))) break;
        continue;
      }
      ok = sess.type(f[1], &text, &err);
    } else if (op == "backspace") {
      ok = sess.backspace(&text, &err);
    } else if (op == "convert-session") {
      ok = sess.convert(&text, &err);
    } else if (op == "preedit") {
      ok = sess.preedit(&text, &err);
    } else if (op == "commit") {
      ok = sess.commit(&text, &err);
      if (ok) {  // commit uses its own reply verb so the client can tell it apart
        if (!write_all(cfd, atzc::join_tabs({"commit", text}))) break;
        continue;
      }
    } else if (op == "cancel") {
      ok = sess.cancel(&err);
      text.clear();
    } else {
      handled = false;
    }
    if (handled) {
      if (!ok) {
        if (!write_all(cfd, atzc::join_tabs({"err", err}))) break;
        continue;
      }
      if (!reply_ok1(cfd, text)) break;
      continue;
    }

    if (!write_all(cfd, atzc::join_tabs({"err", "unknown op: " + op}))) break;
  }
  ::close(cfd);
}

[[noreturn]] void usage(const char *argv0, int code) {
  std::fprintf(code ? stderr : stdout,
               "usage: %s [--engine-dir <path>] [--socket <path>] [--type-delay-ms N]\n"
               "  --engine-dir    harness dir (scripts + native); default: bundled engine/\n"
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
#ifdef ATZC_ENGINE_DIR
    engine_dir = ATZC_ENGINE_DIR;  // bundled engine/ (scripts + native)
#else
    std::fprintf(stderr, "atzcd: --engine-dir is required\n");
    usage(argv[0], 2);
#endif
  }
  if (socket_path.empty()) socket_path = atzc::default_socket_path();
  g_socket_path = socket_path;

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, cleanup_and_exit);
  signal(SIGTERM, cleanup_and_exit);

  // Bring the engine up first; no point opening the socket if it fails.
  atzc::HarnessEngine engine(
      atzc::MakeHarnessProcess(engine_dir, type_delay_ms));
  std::fprintf(stderr, "atzcd: starting engine (cold bring-up can take minutes)...\n");
  if (std::string err; !engine.start(&err)) {
    std::fprintf(stderr, "atzcd: engine failed to start: %s\n", err.c_str());
    return 1;
  }
  std::fprintf(stderr, "atzcd: engine ready\n");
  atzc::Session session(&engine);

  // Stateful-session backend: a second, long-lived keepalive harness for the
  // typing/henkan/commit composition path. It shares the already-warm servers
  // brought up above, so it needs no bringUp() here — it warms its worker
  // lazily on the first session request (keeps cold-start latency to one warm).
  atzc::SessionEngine sess(atzc::MakeSessionHarnessProcess(engine_dir));

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
    serve_conn(cfd, session, sess);
  }

  cleanup_and_exit(0);
}
