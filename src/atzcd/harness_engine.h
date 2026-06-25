// HarnessEngine — drives the Wine-hosted engine over a child process's
// stdin/stdout. start() brings the engine up once and spawns one persistent warm
// worker; topOne()/convert() write a command line and read its result block. The
// worker is transparently respawned if it ever dies.
//
//   -> henkan|convert <romaji>\n
//   <- ATD BEGIN / ATD COMMIT <s> / ATD CAND <s> ... / ATD END   (other lines ignored)

#ifndef ATZCD_HARNESS_ENGINE_H_
#define ATZCD_HARNESS_ENGINE_H_

#include <sys/types.h>

#include <string>

#include "atzcd/engine.h"

namespace atzc {

class HarnessEngine : public Engine {
 public:
  // engine_dir: the engine directory (contains the launcher script).
  // type_delay_ms: per-keystroke typing delay for the engine (0 = none).
  explicit HarnessEngine(std::string engine_dir, int type_delay_ms = 0);
  ~HarnessEngine() override;

  bool start(std::string *err) override;  // bring up + spawn the warm worker
  bool topOne(const std::string &romaji, ConvertResult *out,
              std::string *err) override;  // fast top-1
  bool convert(const std::string &romaji, int max, ConvertResult *out,
               std::string *err) override;  // full candidate list
  void stop() override;  // shut the worker down

 private:
  // Send one command line and read its result block; respawns the worker on a
  // dead pipe / drops it on a garbled stream.
  bool runCmd(const std::string &cmd, ConvertResult *out, std::string *err);
  // Spawn the persistent warm worker and wait for it to report ready.
  bool spawnHarness(std::string *err);
  // Reap/close the current harness (if any).
  void killHarness();
  // Write one command line to the harness; respawn once on a dead pipe.
  bool writeCmd(const std::string &line, std::string *err);
  // Read harness stdout until "ATD END", filling `out`. Returns false on EOF.
  bool readBlock(ConvertResult *out, std::string *err);

  std::string engine_dir_;
  int type_delay_ms_;
  pid_t pid_ = -1;     // warm worker pid (-1 = none)
  int in_fd_ = -1;     // -> worker stdin
  int out_fd_ = -1;    // <- worker stdout
  std::string rbuf_;   // unconsumed stdout bytes
};

}  // namespace atzc

#endif  // ATZCD_HARNESS_ENGINE_H_
