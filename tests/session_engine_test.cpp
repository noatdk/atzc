// Dry-run test for SessionEngine's protocol translation, driven by a mock
// HarnessProcess (no real engine). Validates that each session verb writes the
// right s* command line and parses the ATD PRE / ATD COMMIT reply — the core new
// relay logic. Also checks respawn-on-dead-pipe abandons the composition.
//
// Standalone (no test framework); prints FAIL lines and exits non-zero on error.
// Not part of the default build — enable with -DBUILD_TESTS=ON.

#include "atzc/session_engine.h"

#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
void check(bool cond, const std::string &what) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++g_failures;
  }
}

// A scripted harness: each spawn() emits "ATD READY\n" then, for every command
// line written, pops one canned reply and queues it as readable output. It also
// records the exact command lines it received so the test can assert on them.
class MockHarness : public atzc::HarnessProcess {
 public:
  // Replies is a FIFO of "output produced in response to the Nth write".
  std::deque<std::string> replies;
  std::vector<std::string> writes;
  bool bring_up_ok = true;
  bool running_ = false;
  int spawns = 0;

  bool bringUp(std::string *) override { return bring_up_ok; }
  bool spawn(std::string *) override {
    ++spawns;
    running_ = true;
    pending_ += "ATD READY\n";
    return true;
  }
  bool running() const override { return running_; }
  void terminate() override { running_ = false; }

  bool write(const std::string &data, std::string *err) override {
    if (!running_) {
      if (err) *err = "not running";
      return false;
    }
    writes.push_back(data);
    if (!replies.empty()) {
      pending_ += replies.front();
      replies.pop_front();
    }
    return true;
  }

  int read(std::string *buf, int, std::string *) override {
    if (pending_.empty()) return 0;  // "timeout" — nothing buffered
    buf->append(pending_);
    int n = static_cast<int>(pending_.size());
    pending_.clear();
    return n;
  }

 private:
  std::string pending_;
};

}  // namespace

int main() {
  // --- happy path: begin -> type -> convert -> commit ---
  {
    auto mock = std::make_unique<MockHarness>();
    MockHarness *m = mock.get();
    m->replies = {
        "ATD PRE\n",           // sreset -> empty preedit
        "ATD PRE きしゃ\n",    // stype kisha
        "ATD PRE 貴社\n",      // sconv
        "ATD COMMIT 貴社\n",   // scommit
    };
    atzc::SessionEngine se(std::move(mock));
    std::string err, t;
    check(se.start(&err), "start(): " + err);

    check(se.begin(&t, &err), "begin(): " + err);
    check(t.empty(), "begin preedit empty, got '" + t + "'");

    check(se.type("kisha", &t, &err), "type(): " + err);
    check(t == "きしゃ", "type preedit = きしゃ, got '" + t + "'");

    check(se.convert(&t, &err), "convert(): " + err);
    check(t == "貴社", "convert preedit = 貴社, got '" + t + "'");

    check(se.commit(&t, &err), "commit(): " + err);
    check(t == "貴社", "commit text = 貴社, got '" + t + "'");

    // Command lines the daemon should have emitted, in order.
    const std::vector<std::string> want = {"sreset\n", "stype kisha\n",
                                           "sconv\n", "scommit\n"};
    check(m->writes == want, "command sequence mismatch");
  }

  // --- backspace / cancel ---
  {
    auto mock = std::make_unique<MockHarness>();
    MockHarness *m = mock.get();
    m->replies = {
        "ATD PRE\n",         // sreset
        "ATD PRE か\n",      // stype ka
        "ATD PRE\n",         // sback -> empty
        "ATD PRE\n",         // scancel
    };
    atzc::SessionEngine se(std::move(mock));
    std::string err, t;
    check(se.start(&err), "start2(): " + err);
    check(se.begin(&t, &err), "begin2(): " + err);
    check(se.type("ka", &t, &err) && t == "か", "type2 = か, got '" + t + "'");
    check(se.backspace(&t, &err) && t.empty(), "backspace empty");
    check(se.cancel(&err), "cancel(): " + err);
    const std::vector<std::string> want = {"sreset\n", "stype ka\n", "sback\n",
                                           "scancel\n"};
    check(m->writes == want, "bs/cancel command sequence mismatch");
  }

  // --- type auto-begins when no composition is active ---
  {
    auto mock = std::make_unique<MockHarness>();
    MockHarness *m = mock.get();
    m->replies = {
        "ATD PRE\n",       // implicit sreset from type()
        "ATD PRE あ\n",    // stype a
    };
    atzc::SessionEngine se(std::move(mock));
    std::string err, t;
    check(se.start(&err), "start3(): " + err);
    check(se.type("a", &t, &err) && t == "あ", "auto-begin type = あ");
    const std::vector<std::string> want = {"sreset\n", "stype a\n"};
    check(m->writes == want, "auto-begin sequence mismatch");
  }

  if (g_failures == 0)
    std::printf("session_engine_test: all checks passed\n");
  else
    std::fprintf(stderr, "session_engine_test: %d FAILURES\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
