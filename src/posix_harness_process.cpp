// PosixHarnessProcess — the POSIX/Wine implementation of HarnessProcess.
//
// Spawns `bash -lc` running the engine directory's launcher (`scripts/at.sh`)
// under a headless, warm-reuse Wine environment, wired to pipes. All the
// platform-specific machinery (pipe/posix_spawn/dup2, poll, SIGTERM/SIGKILL)
// lives here; HarnessEngine drives it through the HarnessProcess interface.

#include "atzc/harness_process.h"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

extern char **environ;

namespace atzc {
namespace {

std::string base_env(int type_delay_ms) {
  // Warm-reuse + fast-path flags for the engine; headless (private Xvfb) and quiet.
  std::string env = "env AT_HEADLESS=1 WINEDEBUG=-all"
                    " AT_RECONV_KEEPALIVE=1 AT_FAST_KEYS=1 AT_PUMP_MS=0 AT_QUIET=1";
  env += " AT_TYPE_DELAY_MS=" +
         std::to_string(type_delay_ms > 0 ? type_delay_ms : 0);
  return env;
}

std::string serve_cmd(const std::string &dir, int type_delay_ms) {
  return "cd '" + dir + "' && " + base_env(type_delay_ms) +
         " ./scripts/at.sh daemon-once";
}

std::string up_cmd(const std::string &dir) {
  return "cd '" + dir +
         "' && env AT_HEADLESS=1 WINEDEBUG=-all ./scripts/at.sh daemon-up";
}

class PosixHarnessProcess : public HarnessProcess {
 public:
  PosixHarnessProcess(std::string engine_dir, int type_delay_ms)
      : engine_dir_(std::move(engine_dir)), type_delay_ms_(type_delay_ms) {}
  ~PosixHarnessProcess() override { terminate(); }

  bool bringUp(std::string *err) override {
    // One-time heavy bring-up: prefix + build + warm engine servers.
    if (!runBlocking(up_cmd(engine_dir_), err)) {
      if (err && err->empty()) *err = "daemon-up failed (see its stderr)";
      return false;
    }
    return true;
  }

  bool spawn(std::string *err) override {
    terminate();

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
    return true;
  }

  bool running() const override { return pid_ > 0 && in_fd_ >= 0 && out_fd_ >= 0; }

  void terminate() override {
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

  bool write(const std::string &data, std::string *err) override {
    for (size_t off = 0; off < data.size();) {
      ssize_t n = ::write(in_fd_, data.data() + off, data.size() - off);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) {  // pipe broke: harness died
        if (err) *err = "harness write failed (engine not responding)";
        return false;
      }
      off += static_cast<size_t>(n);
    }
    return true;
  }

  int read(std::string *buf, int timeout_ms, std::string *err) override {
    if (out_fd_ < 0) {
      if (err) *err = "harness not running";
      return -1;
    }
    pollfd pfd{out_fd_, POLLIN, 0};
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr < 0) {
      if (errno == EINTR) return 0;
      if (err) *err = std::string("poll: ") + std::strerror(errno);
      return -1;
    }
    if (pr == 0) return 0;  // timeout
    char tmp[4096];
    ssize_t n = ::read(out_fd_, tmp, sizeof(tmp));
    if (n < 0) {
      if (errno == EINTR) return 0;
      if (err) *err = std::string("read: ") + std::strerror(errno);
      return -1;
    }
    if (n == 0) {  // clean EOF — harness died
      if (err) err->clear();
      return -1;
    }
    buf->append(tmp, static_cast<size_t>(n));
    return static_cast<int>(n);
  }

 private:
  // Run a shell command to completion; true iff it exits 0.
  static bool runBlocking(const std::string &cmd, std::string *err) {
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

  std::string engine_dir_;
  int type_delay_ms_;
  pid_t pid_ = -1;   // warm worker pid (-1 = none)
  int in_fd_ = -1;   // -> worker stdin
  int out_fd_ = -1;  // <- worker stdout
};

}  // namespace

std::unique_ptr<HarnessProcess> MakeHarnessProcess(std::string engine_dir,
                                                   int type_delay_ms) {
  return std::make_unique<PosixHarnessProcess>(std::move(engine_dir),
                                               type_delay_ms);
}

}  // namespace atzc
