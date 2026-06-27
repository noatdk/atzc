#include "atzc/harness_engine.h"

#include <cstddef>
#include <utility>

#include "atzc/proto.h"

namespace atzc {
namespace {

// A warm convert is a few hundred ms; generous ceiling so a wedged backend
// can't hang the daemon. Bring-up (first request after spawn) is slower.
constexpr int kConvertTimeoutMs = 30000;
constexpr int kReadyTimeoutMs = 60000;

}  // namespace

HarnessEngine::HarnessEngine(std::unique_ptr<HarnessProcess> process)
    : proc_(std::move(process)) {}

HarnessEngine::~HarnessEngine() {
  if (proc_) proc_->terminate();
}

bool HarnessEngine::start(std::string *err) {
  if (!proc_->bringUp(err)) return false;
  return ensureWarm(err);  // warm the worker now so the first convert is fast
}

void HarnessEngine::stop() {
  if (proc_) proc_->terminate();
}

// (Re)spawn the worker and consume its stdout until the "ATD READY" sentinel.
bool HarnessEngine::ensureWarm(std::string *err) {
  if (!proc_->spawn(err)) return false;
  rbuf_.clear();
  int waited = 0;
  for (;;) {
    size_t nl;
    while ((nl = rbuf_.find('\n')) != std::string::npos) {
      std::string line = rbuf_.substr(0, nl);
      rbuf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line == "ATD READY") return true;
    }
    std::string rerr;
    int n = proc_->read(&rbuf_, 1000, &rerr);
    if (n < 0) {
      if (err) *err = rerr.empty() ? "harness died during bring-up" : rerr;
      break;
    }
    if (n == 0 && (waited += 1000) >= kReadyTimeoutMs) break;
  }
  if (err && err->empty()) *err = "warm harness did not report ready";
  proc_->terminate();
  return false;
}

bool HarnessEngine::writeCmd(const std::string &line, std::string *err) {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!proc_->running()) {
      if (!ensureWarm(err)) return false;
    }
    if (proc_->write(line, err)) return true;
    proc_->terminate();  // broken pipe: respawn on the next loop
  }
  if (err) *err = "harness write failed (engine not responding)";
  return false;
}

// Read until "ATD END", parsing the ATD block. Returns false on EOF/timeout.
bool HarnessEngine::readBlock(ConvertResult *out, std::string *err) {
  int waited = 0;
  for (;;) {
    size_t nl;
    while ((nl = rbuf_.find('\n')) != std::string::npos) {
      std::string line = rbuf_.substr(0, nl);
      rbuf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.rfind("ATD ", 0) != 0) continue;
      std::string rest = line.substr(4);
      if (rest == "BEGIN") { out->commit.clear(); out->candidates.clear(); }
      else if (rest == "END") return true;
      else if (rest.rfind("COMMIT ", 0) == 0) out->commit = rest.substr(7);
      else if (rest.rfind("CAND ", 0) == 0) out->candidates.push_back(rest.substr(5));
    }
    std::string rerr;
    int n = proc_->read(&rbuf_, 1000, &rerr);
    if (n < 0) {
      if (err) *err = rerr.empty() ? "engine produced no result" : rerr;
      break;
    }
    if (n == 0 && (waited += 1000) >= kConvertTimeoutMs) break;
  }
  if (err && err->empty()) *err = "engine produced no result";
  return false;
}

bool HarnessEngine::runCmd(const std::string &cmd, ConvertResult *out,
                           std::string *err) {
  if (!writeCmd(cmd, err)) return false;
  if (!readBlock(out, err)) {
    proc_->terminate();  // dead/garbled stream: drop it so the next call respawns
    return false;
  }
  return true;
}

bool HarnessEngine::topOne(const std::string &romaji, ConvertResult *out,
                           std::string *err) {
  // `henkan` emits only ATD COMMIT (the top-1); candidates stays empty.
  return runCmd("henkan " + romaji + "\n", out, err);
}

bool HarnessEngine::convert(const std::string &romaji, int max,
                            ConvertResult *out, std::string *err) {
  if (!runCmd("convert " + romaji + "\n", out, err)) return false;
  if (max > 0 && static_cast<int>(out->candidates.size()) > max)
    out->candidates.resize(max);
  return true;
}

}  // namespace atzc
