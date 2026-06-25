#include "atzcd/harness_engine.h"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "atzc/proto.h"

extern char **environ;

namespace atzc {
namespace {

// A warm convert is a few hundred ms; generous ceiling so a wedged backend can't
// hang the daemon. Bring-up (first request after spawn) is slower, so allow more.
constexpr int kConvertTimeoutMs = 30000;
constexpr int kReadyTimeoutMs = 60000;

std::string base_env(int type_delay_ms) {
  // Warm-reuse + fast-path flags for the engine; headless and quiet.
  std::string env = "env -u DISPLAY WINEDEBUG=-all"
                    " AT_RECONV_KEEPALIVE=1 AT_FAST_KEYS=1 AT_PUMP_MS=0 AT_QUIET=1";
  env += " AT_TYPE_DELAY_MS=" + std::to_string(type_delay_ms > 0 ? type_delay_ms : 0);
  return env;
}

std::string serve_cmd(const std::string &dir, int type_delay_ms) {
  return "cd '" + dir + "' && " + base_env(type_delay_ms) +
         " ./scripts/at.sh daemon-once";
}

std::string up_cmd(const std::string &dir) {
  return "cd '" + dir + "' && env -u DISPLAY WINEDEBUG=-all ./scripts/at.sh daemon-up";
}

bool run_blocking(const std::string &cmd, std::string *err) {
  const char *argv[] = {"bash", "-lc", cmd.c_str(), nullptr};
  pid_t pid;
  if (int rc = posix_spawnp(&pid, "bash", nullptr, nullptr,
                            const_cast<char *const *>(argv), environ);
      rc != 0) {
    if (err) *err = std::string("spawn: ") + std::strerror(rc);
    return false;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

HarnessEngine::HarnessEngine(std::string engine_dir, int type_delay_ms)
    : engine_dir_(std::move(engine_dir)), type_delay_ms_(type_delay_ms) {}

HarnessEngine::~HarnessEngine() { killHarness(); }

bool HarnessEngine::start(std::string *err) {
  // One-time heavy bring-up: prefix + build + warm engine servers.
  if (!run_blocking(up_cmd(engine_dir_), err)) {
    if (err && err->empty()) *err = "daemon-up failed (see its stderr)";
    return false;
  }
  return spawnHarness(err);  // bring the warm TIP up now so the first convert is fast
}

void HarnessEngine::stop() { killHarness(); }

// Spawn the persistent warm harness wired to pipes and wait for "ATD READY".
bool HarnessEngine::spawnHarness(std::string *err) {
  killHarness();
  rbuf_.clear();

  int inpipe[2], outpipe[2];
  if (pipe(inpipe) != 0 || pipe(outpipe) != 0) {
    if (err) *err = std::string("pipe: ") + std::strerror(errno);
    return false;
  }
  std::string sh = serve_cmd(engine_dir_, type_delay_ms_);
  const char *argv[] = {"bash", "-lc", sh.c_str(), nullptr};

  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, inpipe[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&fa, outpipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_addclose(&fa, inpipe[1]);
  posix_spawn_file_actions_addclose(&fa, outpipe[0]);

  int rc = posix_spawnp(&pid_, "bash", &fa, nullptr,
                        const_cast<char *const *>(argv), environ);
  posix_spawn_file_actions_destroy(&fa);
  ::close(inpipe[0]);
  ::close(outpipe[1]);
  if (rc != 0) {
    ::close(inpipe[1]);
    ::close(outpipe[0]);
    pid_ = -1;
    if (err) *err = std::string("spawn daemon-once: ") + std::strerror(rc);
    return false;
  }
  in_fd_ = inpipe[1];
  out_fd_ = outpipe[0];

  // Wait for the engine-up sentinel before declaring ready.
  std::string buf;
  int waited = 0;
  for (;;) {
    size_t nl;
    while ((nl = rbuf_.find('\n')) != std::string::npos) {
      std::string line = rbuf_.substr(0, nl);
      rbuf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line == "ATD READY") return true;
    }
    pollfd pfd{out_fd_, POLLIN, 0};
    int pr = poll(&pfd, 1, 1000);
    if (pr < 0) { if (errno == EINTR) continue; break; }
    if (pr == 0) { if ((waited += 1000) >= kReadyTimeoutMs) break; continue; }
    char tmp[4096];
    ssize_t n = ::read(out_fd_, tmp, sizeof(tmp));
    if (n < 0) { if (errno == EINTR) continue; break; }
    if (n == 0) break;  // harness died during bring-up
    rbuf_.append(tmp, static_cast<size_t>(n));
  }
  if (err) *err = "warm harness did not report ready";
  killHarness();
  return false;
}

void HarnessEngine::killHarness() {
  if (in_fd_ >= 0) { ::close(in_fd_); in_fd_ = -1; }
  if (out_fd_ >= 0) { ::close(out_fd_); out_fd_ = -1; }
  if (pid_ > 0) {
    kill(pid_, SIGTERM);
    int st;
    for (int i = 0; i < 20; i++) {  // ~2s grace, then SIGKILL
      pid_t r = waitpid(pid_, &st, WNOHANG);
      if (r == pid_ || (r < 0 && errno == ECHILD)) { pid_ = -1; return; }
      usleep(100 * 1000);
    }
    kill(pid_, SIGKILL);
    while (waitpid(pid_, &st, 0) < 0 && errno == EINTR) {
    }
    pid_ = -1;
  }
}

bool HarnessEngine::writeCmd(const std::string &line, std::string *err) {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (pid_ < 0 || in_fd_ < 0) {
      if (!spawnHarness(err)) return false;
    }
    bool ok = true;
    for (size_t off = 0; off < line.size();) {
      ssize_t n = ::write(in_fd_, line.data() + off, line.size() - off);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ok = false; break; }  // pipe broke: harness died
      off += static_cast<size_t>(n);
    }
    if (ok) return true;
    killHarness();  // respawn on the next loop
  }
  if (err) *err = "harness write failed (engine not responding)";
  return false;
}

// Read until "ATD END", parsing the ATD block. Returns false on EOF/timeout.
bool HarnessEngine::readBlock(ConvertResult *out, std::string *err) {
  int waited = 0;
  for (;;) {
    size_t nl;
    while ((nl = rbuf_.find('\n')) != std::string::npos) {
      std::string line = rbuf_.substr(0, nl);
      rbuf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.rfind("ATD ", 0) != 0) continue;
      std::string rest = line.substr(4);
      if (rest == "BEGIN") { out->commit.clear(); out->candidates.clear(); }
      else if (rest == "END") return true;
      else if (rest.rfind("COMMIT ", 0) == 0) out->commit = rest.substr(7);
      else if (rest.rfind("CAND ", 0) == 0) out->candidates.push_back(rest.substr(5));
    }
    pollfd pfd{out_fd_, POLLIN, 0};
    int pr = poll(&pfd, 1, 1000);
    if (pr < 0) { if (errno == EINTR) continue; break; }
    if (pr == 0) { if ((waited += 1000) >= kConvertTimeoutMs) break; continue; }
    char tmp[4096];
    ssize_t n = ::read(out_fd_, tmp, sizeof(tmp));
    if (n < 0) { if (errno == EINTR) continue; break; }
    if (n == 0) break;  // harness died mid-convert
    rbuf_.append(tmp, static_cast<size_t>(n));
  }
  if (err) *err = "engine produced no result";
  return false;
}

bool HarnessEngine::runCmd(const std::string &cmd, ConvertResult *out,
                           std::string *err) {
  if (!writeCmd(cmd, err)) return false;
  if (!readBlock(out, err)) {
    killHarness();  // dead/garbled stream: drop it so the next call respawns
    return false;
  }
  return true;
}

bool HarnessEngine::topOne(const std::string &romaji, ConvertResult *out,
                           std::string *err) {
  // `henkan` emits only ATD COMMIT (the top-1); candidates stays empty.
  return runCmd("henkan " + romaji + "\n", out, err);
}

bool HarnessEngine::convert(const std::string &romaji, int max,
                            ConvertResult *out, std::string *err) {
  if (!runCmd("convert " + romaji + "\n", out, err)) return false;
  if (max > 0 && static_cast<int>(out->candidates.size()) > max)
    out->candidates.resize(max);
  return true;
}

}  // namespace atzc
