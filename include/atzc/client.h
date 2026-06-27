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
  // daemon's prefetch cache.
  bool candidates(const std::string &romaji, int max, ConvertResult *out,
                  std::string *err) override;

  // Liveness check.
  bool ping(std::string *err);

 private:
  bool request(const char *op, const std::string &romaji, int max,
               ConvertResult *out, std::string *err);
  bool send_line(const std::string &line, std::string *err);
  bool recv_line(std::string *line, std::string *err);

  std::string path_;
  int fd_ = -1;
  std::string rxbuf_;  // carries bytes past one '\n' between recv_line calls
};

}  // namespace atzc

#endif  // ATZC_CLIENT_H_
