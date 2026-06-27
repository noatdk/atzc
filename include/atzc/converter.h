// Converter — the consumer-facing conversion contract, independent of where the
// engine lives. Implemented by:
//   - Client        (talks to a remote atzcd over a Unix socket; Linux)
//   - LocalConverter (drives an in-process Session + Engine; e.g. modore on
//                     Windows, where the engine is native and a daemon/socket
//                     would be needless indirection)
// A consumer can hold an atzc::Converter and not care which transport backs it.

#ifndef ATZC_CONVERTER_H_
#define ATZC_CONVERTER_H_

#include <string>

#include "atzc/proto.h"

namespace atzc {

class Converter {
 public:
  virtual ~Converter() = default;

  // Fast top-1: out->commit set, out->candidates empty.
  virtual bool convert(const std::string &romaji, ConvertResult *out,
                       std::string *err) = 0;

  // Full top-1 + candidate list; max <= 0 = no cap.
  virtual bool candidates(const std::string &romaji, int max,
                          ConvertResult *out, std::string *err) = 0;
};

}  // namespace atzc

#endif  // ATZC_CONVERTER_H_
