// HarnessEngine — drives a harness child that hosts the IME, over the ATD line
// protocol. start() brings the engine up once and spawns one persistent warm
// worker; topOne()/convert() write a command line and read its result block.
// The worker is transparently respawned if it ever dies.
//
//   -> henkan|convert <romaji>\n
//   <- ATD BEGIN / ATD COMMIT <s> / ATD CAND <s> ... / ATD END   (other lines ignored)
//
// The protocol is platform-independent; launching and piping the child is not,
// so that lives behind HarnessProcess (POSIX/Wine today, native Windows later).

#ifndef ATZC_HARNESS_ENGINE_H_
#define ATZC_HARNESS_ENGINE_H_

#include <memory>
#include <string>

#include "atzc/engine.h"
#include "atzc/harness_process.h"

namespace atzc {

class HarnessEngine : public Engine {
 public:
  // Takes ownership of the launched/piped harness child (e.g. from
  // MakePosixHarnessProcess). Construction is cheap; start() does the work.
  explicit HarnessEngine(std::unique_ptr<HarnessProcess> process);
  ~HarnessEngine() override;

  bool start(std::string *err) override;  // bring up + spawn the warm worker
  bool topOne(const std::string &romaji, ConvertResult *out,
              std::string *err) override;  // fast top-1
  bool convert(const std::string &romaji, int max, ConvertResult *out,
               std::string *err) override;  // full candidate list
  void stop() override;  // shut the worker down

 private:
  // (Re)spawn the worker and block until it reports "ATD READY".
  bool ensureWarm(std::string *err);
  // Send one command line and read its result block; respawns the worker on a
  // dead pipe / drops it on a garbled stream.
  bool runCmd(const std::string &cmd, ConvertResult *out, std::string *err);
  // Write one command line to the harness; respawn once on a dead pipe.
  bool writeCmd(const std::string &line, std::string *err);
  // Read harness stdout until "ATD END", filling `out`. Returns false on EOF.
  bool readBlock(ConvertResult *out, std::string *err);

  std::unique_ptr<HarnessProcess> proc_;
  std::string rbuf_;  // unconsumed harness stdout bytes (line-buffered parse)
};

}  // namespace atzc

#endif  // ATZC_HARNESS_ENGINE_H_
