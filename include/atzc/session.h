// Session — the split conversion model in front of the (serial) Engine.
//
//   top1(romaji)       fast top-1, returned immediately, AND schedules a
//                      background prefetch of the full candidate list.
//   candidates(romaji) the full list — served from cache (usually already
//                      prefetched by the time the user opens the candidate
//                      window) or computed on a miss.
//
// The engine does one conversion at a time, so a mutex serializes access;
// foreground calls take priority over the background prefetch. The top-1 returns
// fast (~0.08 s) while the slower full list is computed off the client's critical
// path and ready when needed.

#ifndef ATZC_SESSION_H_
#define ATZC_SESSION_H_

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "atzc/proto.h"

namespace atzc {

class Engine;

class Session {
 public:
  explicit Session(Engine *engine);
  ~Session();

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  // Fast top-1; also kicks off the background candidate prefetch.
  bool top1(const std::string &romaji, ConvertResult *out, std::string *err);
  // Full candidate list; cache hit when the prefetch already ran.
  bool candidates(const std::string &romaji, int max, ConvertResult *out,
                  std::string *err);

 private:
  void prefetchLoop();
  bool cacheGet(const std::string &romaji, ConvertResult *out);
  bool cacheHas(const std::string &romaji);
  void cachePut(const std::string &romaji, const ConvertResult &r);
  // Remember/take the top-1 for a reading (set by top1(), consumed when building
  // its candidate list) so the list leads with the same top candidate as the
  // inline conversion.
  void rememberTop1(const std::string &romaji, const std::string &top1);
  std::string takeTop1(const std::string &romaji);
  // Build the candidate list: top-1 first, then the full-list alternatives
  // (deduped). Computes the top-1 via the engine if not remembered.
  bool buildCandidates(const std::string &romaji, ConvertResult *out,
                       std::string *err);

  Engine *engine_;
  std::mutex engine_mu_;             // serializes all engine access
  std::atomic<int> fg_waiting_{0};   // foreground calls waiting for the engine

  std::mutex cache_mu_;
  std::unordered_map<std::string, ConvertResult> cache_;
  std::deque<std::string> lru_;
  std::unordered_map<std::string, std::string> top1_;  // reading -> top-1 hint
  static constexpr size_t kCacheMax = 64;

  std::mutex q_mu_;
  std::condition_variable q_cv_;
  std::string pending_;              // romaji awaiting prefetch ("" = none)
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace atzc

#endif  // ATZC_SESSION_H_
