// HarnessProcess — owns the launched harness child and its stdio pipes.
//
// This is the one platform-specific seam under HarnessEngine: it abstracts
// process spawn, pipe wiring, liveness, and termination, so the same ATD line
// protocol drives the IME whether the harness runs under Wine (POSIX) or
// natively (a future Windows launch). Reads/writes are raw bytes; the line and
// ATD framing are HarnessEngine's job.

#ifndef ATZC_HARNESS_PROCESS_H_
#define ATZC_HARNESS_PROCESS_H_

#include <memory>
#include <string>

namespace atzc {

class HarnessProcess {
 public:
  virtual ~HarnessProcess() = default;

  // One-time heavy bring-up (e.g. build the Wine prefix + warm servers). May be
  // a no-op. Returns false / sets *err on failure.
  virtual bool bringUp(std::string *err) = 0;

  // (Re)launch the worker and wire its stdin/stdout, terminating any previous
  // instance first. Returns false / sets *err on failure. Does not wait for the
  // engine's readiness sentinel — that is protocol, and HarnessEngine's job.
  virtual bool spawn(std::string *err) = 0;

  // Whether a worker is currently running.
  virtual bool running() const = 0;

  // Terminate the worker (graceful, then forceful) and reap it. Idempotent.
  virtual void terminate() = 0;

  // Write all bytes to the worker's stdin. Returns false / sets *err on a
  // broken pipe (the worker died).
  virtual bool write(const std::string &data, std::string *err) = 0;

  // Append whatever stdout bytes are available, waiting up to timeout_ms.
  // Returns >0 (bytes appended), 0 (timeout / interrupted), or -1 (EOF or
  // error: *err set on error, left empty on a clean EOF).
  virtual int read(std::string *buf, int timeout_ms, std::string *err) = 0;
};

// The platform's default harness process, selected at compile time:
//   - POSIX:   spawns `bash -lc` running <engine_dir>/scripts/at.sh under a
//              headless Wine environment.
//   - Windows: launches <engine_dir>'s native launcher directly (no Wine).
// `engine_dir` is UTF-8; `type_delay_ms` is the per-keystroke typing delay.
std::unique_ptr<HarnessProcess> MakeHarnessProcess(std::string engine_dir,
                                                   int type_delay_ms);

}  // namespace atzc

#endif  // ATZC_HARNESS_PROCESS_H_
