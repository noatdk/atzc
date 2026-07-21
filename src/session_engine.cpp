#include "atzc/session_engine.h"

#include <cstddef>
#include <utility>

namespace atzc {
namespace {

// A warm session command (type/convert/commit) settles in a few hundred ms;
// generous ceiling so a wedged backend can't hang the daemon. Bring-up (the
// first "ATD READY" after spawn) is slower.
constexpr int kReplyTimeoutMs = 30000;
constexpr int kReadyTimeoutMs = 60000;

}  // namespace

SessionEngine::SessionEngine(std::unique_ptr<HarnessProcess> process)
    : proc_(std::move(process)) {}

SessionEngine::~SessionEngine() {
  if (proc_) proc_->terminate();
}

bool SessionEngine::start(std::string *err) {
  if (!proc_->bringUp(err)) return false;
  return ensureWarm(err);
}

void SessionEngine::stop() {
  if (proc_) proc_->terminate();
  active_ = false;
}

// (Re)spawn the session worker and consume its stdout until "ATD READY".
bool SessionEngine::ensureWarm(std::string *err) {
  if (!proc_->spawn(err)) return false;
  rbuf_.clear();
  active_ = false;
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
      if (err) *err = rerr.empty() ? "session harness died during bring-up" : rerr;
      break;
    }
    if (n == 0 && (waited += 1000) >= kReadyTimeoutMs) break;
  }
  if (err && err->empty()) *err = "session harness did not report ready";
  proc_->terminate();
  return false;
}

bool SessionEngine::writeCmd(const std::string &line, std::string *err) {
  if (!proc_->running()) {
    // Worker gone: warm a fresh one. Any in-flight composition is lost, so the
    // caller must re-begin (runCmd surfaces this).
    if (!ensureWarm(err)) return false;
  }
  if (proc_->write(line, err)) return true;
  proc_->terminate();
  active_ = false;
  if (err && err->empty()) *err = "session harness write failed";
  return false;
}

// Read one reply line for the command just sent: "ATD PRE <text>" (or
// "ATD COMMIT <text>" when want_commit). Other lines are ignored.
bool SessionEngine::readReply(bool want_commit, std::string *text,
                              std::string *err) {
  const char *marker = want_commit ? "ATD COMMIT " : "ATD PRE ";
  const size_t mlen = want_commit ? 11 : 8;  // strlen of the marker above
  int waited = 0;
  for (;;) {
    size_t nl;
    while ((nl = rbuf_.find('\n')) != std::string::npos) {
      std::string line = rbuf_.substr(0, nl);
      rbuf_.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      // "ATD PRE" with no trailing space is the empty-preedit reply.
      if (!want_commit && line == "ATD PRE") {
        if (text) text->clear();
        return true;
      }
      if (line.rfind(marker, 0) == 0) {
        if (text) *text = line.substr(mlen);
        return true;
      }
      // Ignore any other ATD/log lines and keep reading.
    }
    std::string rerr;
    int n = proc_->read(&rbuf_, 1000, &rerr);
    if (n < 0) {
      if (err) *err = rerr.empty() ? "session harness produced no reply" : rerr;
      break;
    }
    if (n == 0 && (waited += 1000) >= kReplyTimeoutMs) break;
  }
  if (err && err->empty()) *err = "session harness produced no reply";
  return false;
}

bool SessionEngine::runCmd(const std::string &line, bool want_commit,
                           std::string *text, std::string *err) {
  if (!writeCmd(line, err)) return false;
  if (!readReply(want_commit, text, err)) {
    proc_->terminate();  // dead/garbled: drop it so the next begin() respawns
    active_ = false;
    return false;
  }
  return true;
}

bool SessionEngine::begin(std::string *preedit, std::string *err) {
  bool ok = runCmd("sreset\n", /*want_commit=*/false, preedit, err);
  active_ = ok;
  return ok;
}

bool SessionEngine::type(const std::string &ascii, std::string *preedit,
                         std::string *err) {
  if (!active_ && !begin(nullptr, err)) return false;
  return runCmd("stype " + ascii + "\n", /*want_commit=*/false, preedit, err);
}

bool SessionEngine::backspace(std::string *preedit, std::string *err) {
  if (!active_) {
    if (preedit) preedit->clear();
    return true;  // nothing to delete
  }
  return runCmd("sback\n", /*want_commit=*/false, preedit, err);
}

bool SessionEngine::convert(std::string *preedit, std::string *err) {
  if (!active_) {
    if (err) *err = "convert: no active composition";
    return false;
  }
  return runCmd("sconv\n", /*want_commit=*/false, preedit, err);
}

bool SessionEngine::preedit(std::string *out, std::string *err) {
  if (!active_) {
    if (out) out->clear();
    return true;
  }
  return runCmd("spre\n", /*want_commit=*/false, out, err);
}

bool SessionEngine::commit(std::string *committed, std::string *err) {
  if (!active_) {
    if (committed) committed->clear();
    return true;  // nothing to commit
  }
  bool ok = runCmd("scommit\n", /*want_commit=*/true, committed, err);
  active_ = false;  // committed or failed, the composition is over
  return ok;
}

bool SessionEngine::cancel(std::string *err) {
  if (!active_ && proc_->running()) return true;
  bool ok = runCmd("scancel\n", /*want_commit=*/false, nullptr, err);
  active_ = false;
  return ok;
}

}  // namespace atzc
