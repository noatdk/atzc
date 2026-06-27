// The conversion engine behind the daemon. One implementation today
// (HarnessEngine, which drives the Wine-hosted TSF harness over stdio); the
// interface keeps the socket server and the engine transport decoupled.

#ifndef ATZC_ENGINE_H_
#define ATZC_ENGINE_H_

#include <string>

#include "atzc/proto.h"

namespace atzc {

class Engine {
 public:
  virtual ~Engine() = default;

  // Bring the engine up and block until it is ready to convert. May take a few
  // minutes on a cold Wine prefix. Returns false / sets *err on failure.
  virtual bool start(std::string *err) = 0;

  // Fast top-1 only (the latency-critical inline-conversion path): out->commit is
  // the result, out->candidates is empty.
  virtual bool topOne(const std::string &romaji, ConvertResult *out,
                      std::string *err) = 0;

  // Full top-1 + candidate list (the alternatives). max <= 0 means "no cap";
  // slower than topOne(). The engine transparently recovers from a backend crash:
  // a call returns a result or a clear error, never leaves the engine wedged.
  virtual bool convert(const std::string &romaji, int max, ConvertResult *out,
                       std::string *err) = 0;

  virtual void stop() = 0;
};

}  // namespace atzc

#endif  // ATZC_ENGINE_H_
