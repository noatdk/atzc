// libatzcclient — the client side of the atzcd socket protocol.
//
// Shared verbatim by the fcitx5 addon, the ibus engine (via its own binding),
// the CLI, and modore's backend. Connect once, call convert() per request.
// Not thread-safe; use one Client per thread (or serialize).

#ifndef ATZC_CLIENT_H_
#define ATZC_CLIENT_H_

#include <string>

#include "atzc/converter.h"
#include "atzc/proto.h"

namespace atzc {

// Default socket path: $ATZC_SOCKET, else $XDG_RUNTIME_DIR/atzcd.sock, else
// /tmp/atzcd-<uid>.sock. Kept in one place so every consumer agrees.
std::string default_socket_path();

class Client : public Converter {
 public:
  // Empty path -> default_socket_path().
  explicit Client(std::string socket_path = "");
  ~Client();

  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  // Connect to the daemon. Returns false and sets *err on failure.
  bool connect(std::string *err);
  bool connected() const { return fd_ >= 0; }
  void close();

  // Fast top-1 (out->commit set; out->candidates empty). The daemon prefetches the
  // full list in the background so a following candidates() call is usually instant.
  bool convert(const std::string &romaji, ConvertResult *out,
               std::string *err) override;

  // Full top-1 + candidate list; max <= 0 = no cap. Typically served from the
  // daemon's prefetch cache. out->candidates holds the flat surfaces.
  bool candidates(const std::string &romaji, int max, ConvertResult *out,
                  std::string *err) override;

  // Enriched full list: out->candidatesEx holds (surface, reading, hinsi) per
  // candidate — cleaner and wider than the flat list. out->commit is the top-1.
  // Falls back to flat surfaces (empty reading/hinsi) if the engine gave none.
  bool candidatesEx(const std::string &romaji, int max, ConvertResult *out,
                    std::string *err);

  // Liveness check.
  bool ping(std::string *err);

  // --- stateful session API (the composition path over the keepalive harness) ---
  // A session is one long-lived composition on the daemon: begin(), then feed
  // keystrokes/edits and read back the live preedit, until commit() or cancel().
  // *preedit / *committed receive the resulting text (kana / kanji / committed).
  //
  // sessionBegin      start (or reset) a composition; *preedit emptied.
  // sessionKey        append romaji keystrokes; *preedit is the new reading.
  // sessionBackspace  delete one kana; *preedit is the reading after.
  // sessionConvert    henkan in place; *preedit is the kanji surface.
  // sessionPreedit    read the current preedit without changing it.
  // sessionCommit     finalize; *committed is the committed text (comp ends).
  // sessionCancel     abandon the composition.
  // sessionCandidates the full candidate list for a reading (heavy path; served
  //                   by the daemon's single-shot mechanism, selection client-side).
  bool sessionBegin(std::string *preedit, std::string *err);
  bool sessionKey(const std::string &ascii, std::string *preedit,
                  std::string *err);
  bool sessionBackspace(std::string *preedit, std::string *err);
  bool sessionConvert(std::string *preedit, std::string *err);
  bool sessionPreedit(std::string *preedit, std::string *err);
  bool sessionCommit(std::string *committed, std::string *err);
  bool sessionCancel(std::string *err);
  bool sessionCandidates(const std::string &romaji, int max, ConvertResult *out,
                         std::string *err);

 private:
  bool request(const char *op, const std::string &romaji, int max,
               ConvertResult *out, std::string *err);
  // Send a bare op (optionally one arg field) and read one reply. On success
  // *text receives the reply's payload field (empty if none). The reply verb
  // must be `expect_verb` (e.g. "ok" or "commit").
  bool simpleReq(const std::string &op, const std::string *arg,
                 const char *expect_verb, std::string *text, std::string *err);
  bool send_line(const std::string &line, std::string *err);
  bool recv_line(std::string *line, std::string *err);

  std::string path_;
  int fd_ = -1;
  std::string rxbuf_;  // carries bytes past one '\n' between recv_line calls
};

}  // namespace atzc

#endif  // ATZC_CLIENT_H_
