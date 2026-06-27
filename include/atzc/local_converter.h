// LocalConverter — an in-process Converter: owns an Engine and the Session that
// fronts it (cache + background prefetch), so a host gets the same
// convert/candidates contract as the socket Client without a daemon or socket.
//
// Intended for the single-consumer, native-engine case (modore on Windows): no
// IPC hop, the warm engine lives for the host's lifetime. On Linux, where one
// warm engine is shared across fcitx5/ibus/modore/cli, prefer atzcd + Client.

#ifndef ATZC_LOCAL_CONVERTER_H_
#define ATZC_LOCAL_CONVERTER_H_

#include <memory>
#include <string>

#include "atzc/converter.h"
#include "atzc/engine.h"
#include "atzc/session.h"

namespace atzc {

class LocalConverter : public Converter {
 public:
  // Takes ownership of the engine (e.g. a HarnessEngine). Cheap; call start()
  // to bring the engine up (slow on a cold prefix) before converting.
  explicit LocalConverter(std::unique_ptr<Engine> engine);

  bool start(std::string *err);

  bool convert(const std::string &romaji, ConvertResult *out,
               std::string *err) override;
  bool candidates(const std::string &romaji, int max, ConvertResult *out,
                  std::string *err) override;

 private:
  // engine_ is declared before session_ so it outlives the Session, which holds
  // a raw Engine* and a worker thread that calls into it.
  std::unique_ptr<Engine> engine_;
  Session session_;
};

}  // namespace atzc

#endif  // ATZC_LOCAL_CONVERTER_H_
