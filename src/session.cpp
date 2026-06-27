#include "atzc/session.h"

#include <chrono>

#include "atzc/engine.h"

namespace atzc {
namespace {
void cap(ConvertResult *r, int max) {
  if (max > 0 && static_cast<int>(r->candidates.size()) > max)
    r->candidates.resize(static_cast<size_t>(max));
}
}  // namespace

Session::Session(Engine *engine) : engine_(engine) {
  worker_ = std::thread([this] { prefetchLoop(); });
}

Session::~Session() {
  {
    std::lock_guard<std::mutex> lk(q_mu_);
    stop_ = true;
  }
  q_cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool Session::cacheGet(const std::string &romaji, ConvertResult *out) {
  std::lock_guard<std::mutex> lk(cache_mu_);
  auto it = cache_.find(romaji);
  if (it == cache_.end()) return false;
  *out = it->second;
  return true;
}

bool Session::cacheHas(const std::string &romaji) {
  std::lock_guard<std::mutex> lk(cache_mu_);
  return cache_.find(romaji) != cache_.end();
}

void Session::cachePut(const std::string &romaji, const ConvertResult &r) {
  std::lock_guard<std::mutex> lk(cache_mu_);
  if (cache_.find(romaji) == cache_.end()) {
    lru_.push_back(romaji);
    while (lru_.size() > kCacheMax) {
      cache_.erase(lru_.front());
      lru_.pop_front();
    }
  }
  cache_[romaji] = r;
}

void Session::rememberTop1(const std::string &romaji, const std::string &top1) {
  std::lock_guard<std::mutex> lk(cache_mu_);
  if (top1_.size() > kCacheMax) top1_.clear();  // hint map; safe to drop
  top1_[romaji] = top1;
}

std::string Session::takeTop1(const std::string &romaji) {
  std::lock_guard<std::mutex> lk(cache_mu_);
  auto it = top1_.find(romaji);
  if (it == top1_.end()) return {};
  std::string v = it->second;
  top1_.erase(it);
  return v;
}

// Must be called holding engine_mu_. Lead the list with the top-1 (from a prior
// top1(), or computed now) so the candidate window matches the inline conversion;
// the full-list results follow as the alternatives (deduped).
bool Session::buildCandidates(const std::string &romaji, ConvertResult *out,
                              std::string *err) {
  std::string t1 = takeTop1(romaji);
  if (t1.empty()) {
    ConvertResult tr;
    std::string e;
    if (engine_->topOne(romaji, &tr, &e)) t1 = tr.commit;  // no prior convert
  }
  ConvertResult rlist;
  if (!engine_->convert(romaji, 0, &rlist, err)) return false;

  out->candidates.clear();
  out->commit = !t1.empty() ? t1 : rlist.commit;
  if (!out->commit.empty()) out->candidates.push_back(out->commit);
  for (const auto &c : rlist.candidates)
    if (c != out->commit) out->candidates.push_back(c);
  return true;
}

bool Session::top1(const std::string &romaji, ConvertResult *out,
                   std::string *err) {
  fg_waiting_.fetch_add(1);
  bool ok;
  {
    std::lock_guard<std::mutex> lk(engine_mu_);
    fg_waiting_.fetch_sub(1);
    ok = engine_->topOne(romaji, out, err);
  }
  if (!ok) return false;
  // Remember the top-1 and prefetch the full list (unless already cached) so the
  // candidate window is ready — and consistent with this top-1 — when opened.
  rememberTop1(romaji, out->commit);
  if (!cacheHas(romaji)) {
    {
      std::lock_guard<std::mutex> lk(q_mu_);
      pending_ = romaji;
    }
    q_cv_.notify_one();
  }
  return true;
}

bool Session::candidates(const std::string &romaji, int max, ConvertResult *out,
                         std::string *err) {
  if (cacheGet(romaji, out)) {
    cap(out, max);
    return true;
  }
  fg_waiting_.fetch_add(1);
  std::unique_lock<std::mutex> lk(engine_mu_);
  fg_waiting_.fetch_sub(1);
  // A prefetch for this romaji may have finished while we waited for the lock.
  if (cacheGet(romaji, out)) {
    lk.unlock();
    cap(out, max);
    return true;
  }
  bool ok = buildCandidates(romaji, out, err);
  lk.unlock();
  if (!ok) return false;
  cachePut(romaji, *out);
  cap(out, max);
  return true;
}

void Session::prefetchLoop() {
  for (;;) {
    std::string romaji;
    {
      std::unique_lock<std::mutex> lk(q_mu_);
      q_cv_.wait(lk, [this] { return stop_ || !pending_.empty(); });
      if (stop_) return;
      romaji = pending_;
      pending_.clear();
    }
    if (cacheHas(romaji)) continue;
    // Give foreground requests priority: don't grab the engine while one is
    // waiting (a new convert must not queue behind a background list fetch).
    for (int i = 0; i < 200 && fg_waiting_.load() > 0; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(2));

    ConvertResult r;
    std::string err;
    {
      std::lock_guard<std::mutex> lk(engine_mu_);
      if (cacheHas(romaji)) continue;
      if (!buildCandidates(romaji, &r, &err)) continue;  // drop on failure
    }
    cachePut(romaji, r);
  }
}

}  // namespace atzc
