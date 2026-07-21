// Dry-run test for the enriched-candidate (ATD CANDX) parsing in the batch path,
// driven by a mock HarnessProcess (no real engine). Validates that:
//   1. readBlock parses `ATD CANDX <surface>\t<reading>\t<hinsi>` (arriving
//      before ATD END) into ConvertResult::candidatesEx.
//   2. A barren reconversion (only CANDX, no plain CAND) still yields candidates.
//   3. convert()'s max cap applies to candidatesEx too.
//
// Standalone (no test framework); prints FAIL lines and exits non-zero on error.
// Not part of the default build — enable with -DBUILD_TESTS=ON.

#include "atzc/harness_engine.h"

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

// A scripted harness: spawn() emits "ATD READY"; each command line written pops
// one canned reply block and queues it as readable output.
class MockHarness : public atzc::HarnessProcess {
 public:
  std::deque<std::string> replies;
  std::vector<std::string> writes;
  bool running_ = false;

  bool bringUp(std::string *) override { return true; }
  bool spawn(std::string *) override {
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
    if (pending_.empty()) return 0;
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
  // --- CANDX in a normal convert block (CAND + CANDX both present) ---
  {
    auto mock = std::make_unique<MockHarness>();
    MockHarness *m = mock.get();
    // convert() emits a block with a flat CAND list and an enriched CANDX list.
    m->replies = {
        "ATD BEGIN\n"
        "ATD COMMIT 機械\n"
        "ATD CAND 機械\n"
        "ATD CAND 機会\n"
        "ATD SEG きかい\n"
        "ATD CANDX 機械\tきかい\t名詞\n"
        "ATD CANDX 機会\tきかい\t名詞\n"
        "ATD CANDX 器械\tきかい\t0x08\n"
        "ATD END\n",
    };
    atzc::HarnessEngine eng(std::move(mock));
    std::string err;
    check(eng.start(&err), "start(): " + err);

    atzc::ConvertResult r;
    check(eng.convert("kikai", 0, &r, &err), "convert(): " + err);
    check(r.commit == "機械", "commit = 機械, got '" + r.commit + "'");
    check(r.candidates.size() == 2, "flat candidates = 2");
    check(r.candidatesEx.size() == 3,
          "candidatesEx = 3, got " + std::to_string(r.candidatesEx.size()));
    if (r.candidatesEx.size() == 3) {
      check(r.candidatesEx[0].surface == "機械", "ex[0].surface = 機械");
      check(r.candidatesEx[0].reading == "きかい", "ex[0].reading = きかい");
      check(r.candidatesEx[0].hinsi == "名詞", "ex[0].hinsi = 名詞");
      check(r.candidatesEx[2].surface == "器械", "ex[2].surface = 器械");
      check(r.candidatesEx[2].hinsi == "0x08", "ex[2].hinsi hex fallback = 0x08");
    }
  }

  // --- barren reconversion: NO plain CAND, only CANDX -> still yields cands ---
  {
    auto mock = std::make_unique<MockHarness>();
    MockHarness *m = mock.get();
    m->replies = {
        "ATD BEGIN\n"
        "ATD COMMIT 交渉\n"
        "ATD CANDX 交渉\tこうしょう\t名詞\n"
        "ATD CANDX 校章\tこうしょう\t名詞\n"
        "ATD CANDX 高尚\tこうしょう\t形容動詞\n"
        "ATD END\n",
    };
    atzc::HarnessEngine eng(std::move(mock));
    std::string err;
    check(eng.start(&err), "start2(): " + err);

    atzc::ConvertResult r;
    check(eng.convert("koushou", 0, &r, &err), "convert2(): " + err);
    check(r.candidates.empty(), "flat candidates empty (barren reconversion)");
    check(r.candidatesEx.size() == 3,
          "candidatesEx = 3 despite empty flat list, got " +
              std::to_string(r.candidatesEx.size()));
    if (r.candidatesEx.size() == 3)
      check(r.candidatesEx[1].surface == "校章", "ex[1].surface = 校章");
  }

  // --- max cap applies to candidatesEx ---
  {
    auto mock = std::make_unique<MockHarness>();
    MockHarness *m = mock.get();
    m->replies = {
        "ATD BEGIN\n"
        "ATD COMMIT 交渉\n"
        "ATD CANDX 交渉\tこうしょう\t名詞\n"
        "ATD CANDX 校章\tこうしょう\t名詞\n"
        "ATD CANDX 高尚\tこうしょう\t形容動詞\n"
        "ATD END\n",
    };
    atzc::HarnessEngine eng(std::move(mock));
    std::string err;
    check(eng.start(&err), "start3(): " + err);

    atzc::ConvertResult r;
    check(eng.convert("koushou", 2, &r, &err), "convert3(): " + err);
    check(r.candidatesEx.size() == 2,
          "candidatesEx capped to 2, got " +
              std::to_string(r.candidatesEx.size()));
  }

  if (g_failures == 0)
    std::printf("harness_candx_test: all checks passed\n");
  else
    std::fprintf(stderr, "harness_candx_test: %d FAILURES\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
