// MakeConverter for Windows: an in-process converter driving the installed IME
// via WindowsImeEngine (TSF). No daemon, no socket — modore on Windows links
// this directly. The engine is activated lazily on the first conversion (TSF
// activation is the slow step), matching the Converter contract and the POSIX
// reconnecting-client's lazy-connect.

#ifdef _WIN32

#include "atzc/converter.h"
#include "atzc/local_converter.h"
#include "atzc/windows_ime_engine.h"

#include <memory>
#include <string>
#include <utility>

namespace atzc {
namespace {

// Wraps LocalConverter and brings the engine up on first use.
class LazyLocalConverter : public Converter {
 public:
  explicit LazyLocalConverter(std::unique_ptr<Engine> engine)
      : inner_(std::move(engine)) {}

  bool convert(const std::string &romaji, ConvertResult *out,
               std::string *err) override {
    return ensure_started(err) && inner_.convert(romaji, out, err);
  }

  bool candidates(const std::string &romaji, int max, ConvertResult *out,
                  std::string *err) override {
    return ensure_started(err) && inner_.candidates(romaji, max, out, err);
  }

 private:
  bool ensure_started(std::string *err) {
    if (started_) return true;
    if (!inner_.start(err)) return false;  // TSF activation; slow, may fail
    started_ = true;
    return true;
  }

  LocalConverter inner_;  // owns the engine + its Session
  bool started_ = false;
};

}  // namespace

std::unique_ptr<Converter> MakeConverter(const ConverterConfig &config) {
  (void)config;  // socket_path is POSIX-only; type_delay_ms is unused here today
  return std::make_unique<LazyLocalConverter>(MakeWindowsImeEngine());
}

}  // namespace atzc

#endif  // _WIN32
