#include "atzc/local_converter.h"

#include <utility>

namespace atzc {

LocalConverter::LocalConverter(std::unique_ptr<Engine> engine)
    : engine_(std::move(engine)), session_(engine_.get()) {}

bool LocalConverter::start(std::string *err) { return engine_->start(err); }

bool LocalConverter::convert(const std::string &romaji, ConvertResult *out,
                             std::string *err) {
  // Fast top-1; also kicks off the background candidate prefetch.
  return session_.top1(romaji, out, err);
}

bool LocalConverter::candidates(const std::string &romaji, int max,
                                ConvertResult *out, std::string *err) {
  return session_.candidates(romaji, max, out, err);
}

}  // namespace atzc
