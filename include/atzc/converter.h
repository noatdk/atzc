// Converter — the consumer-facing conversion contract, independent of where the
// engine lives. Implemented by:
//   - Client        (talks to a remote atzcd over a Unix socket; Linux)
//   - LocalConverter (drives an in-process Session + Engine; e.g. modore on
//                     Windows, where the engine is native and a daemon/socket
//                     would be needless indirection)
// A consumer can hold an atzc::Converter and not care which transport backs it.

#ifndef ATZC_CONVERTER_H_
#define ATZC_CONVERTER_H_

#include <memory>
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

// Options for MakeConverter; fields apply only to the platform that uses them.
struct ConverterConfig {
  // POSIX: atzcd socket path ("" -> default_socket_path()). Unused on Windows.
  std::string socket_path;
  // Windows in-process engine: per-keystroke typing delay hint. Unused on POSIX
  // (the daemon owns the engine and its own timing).
  int type_delay_ms = 0;
};

// The single entry point a host uses to obtain a Converter without knowing the
// transport. Returns the platform's implementation, ready to use (it
// lazy-connects / lazy-starts on the first call and recovers once from a
// dropped connection or dead engine):
//   - POSIX:   a reconnecting client to atzcd.
//   - Windows: an in-process engine driving the installed IME.
// Not thread-safe — serialize calls on the returned Converter (the engine is
// serial anyway). Defined per platform (frontend_posix.cpp / frontend_windows.cpp).
std::unique_ptr<Converter> MakeConverter(const ConverterConfig &config = {});

}  // namespace atzc

#endif  // ATZC_CONVERTER_H_
