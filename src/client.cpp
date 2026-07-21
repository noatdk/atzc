#include "atzc/client.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace atzc {

std::string default_socket_path() {
  if (const char *s = std::getenv("ATZC_SOCKET"); s && *s) return s;
  if (const char *r = std::getenv("XDG_RUNTIME_DIR"); r && *r)
    return std::string(r) + "/atzcd.sock";
  return "/tmp/atzcd-" + std::to_string(getuid()) + ".sock";
}

Client::Client(std::string socket_path)
    : path_(socket_path.empty() ? default_socket_path() : std::move(socket_path)) {}

Client::~Client() { close(); }

void Client::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  rxbuf_.clear();
}

bool Client::connect(std::string *err) {
  close();
  fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) {
    if (err) *err = std::string("socket: ") + std::strerror(errno);
    return false;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path_.size() >= sizeof(addr.sun_path)) {
    if (err) *err = "socket path too long: " + path_;
    close();
    return false;
  }
  std::strcpy(addr.sun_path, path_.c_str());
  if (::connect(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    if (err) *err = "connect " + path_ + ": " + std::strerror(errno);
    close();
    return false;
  }
  return true;
}

bool Client::send_line(const std::string &line, std::string *err) {
  size_t off = 0;
  while (off < line.size()) {
    ssize_t n = ::write(fd_, line.data() + off, line.size() - off);
    if (n <= 0) {
      if (errno == EINTR) continue;
      if (err) *err = std::string("write: ") + std::strerror(errno);
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return true;
}

bool Client::recv_line(std::string *line, std::string *err) {
  for (;;) {
    if (auto nl = rxbuf_.find('\n'); nl != std::string::npos) {
      *line = rxbuf_.substr(0, nl);
      rxbuf_.erase(0, nl + 1);
      return true;
    }
    char buf[1024];
    ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR) continue;
      if (err) *err = std::string("read: ") + std::strerror(errno);
      return false;
    }
    if (n == 0) {
      if (err) *err = "connection closed by daemon";
      return false;
    }
    rxbuf_.append(buf, static_cast<size_t>(n));
  }
}

bool Client::request(const char *op, const std::string &romaji, int max,
                     ConvertResult *out, std::string *err) {
  if (fd_ < 0 && !connect(err)) return false;
  std::vector<std::string> req{op, romaji};
  if (max > 0) req.push_back(std::to_string(max));
  if (!send_line(join_tabs(req), err)) return false;

  std::string line;
  if (!recv_line(&line, err)) return false;
  auto f = split_tabs(line);
  if (f.empty()) {
    if (err) *err = "empty reply";
    return false;
  }
  if (f[0] == "err") {
    if (err) *err = f.size() > 1 ? f[1] : "engine error";
    return false;
  }
  if (f[0] != "ok") {
    if (err) *err = "unexpected reply: " + f[0];
    return false;
  }
  out->commit = f.size() > 1 ? f[1] : "";
  out->candidates.assign(f.begin() + (f.size() > 1 ? 2 : 1), f.end());
  out->candidatesEx.clear();
  return true;
}

bool Client::convert(const std::string &romaji, ConvertResult *out,
                     std::string *err) {
  return request("convert", romaji, 0, out, err);
}

bool Client::candidates(const std::string &romaji, int max, ConvertResult *out,
                        std::string *err) {
  return request("candidates", romaji, max, out, err);
}

bool Client::candidatesEx(const std::string &romaji, int max,
                          ConvertResult *out, std::string *err) {
  if (fd_ < 0 && !connect(err)) return false;
  std::vector<std::string> req{"candidates-ex", romaji};
  if (max > 0) req.push_back(std::to_string(max));
  if (!send_line(join_tabs(req), err)) return false;

  std::string line;
  if (!recv_line(&line, err)) return false;
  auto f = split_tabs(line);
  if (f.empty()) {
    if (err) *err = "empty reply";
    return false;
  }
  if (f[0] == "err") {
    if (err) *err = f.size() > 1 ? f[1] : "engine error";
    return false;
  }
  if (f[0] != "okx") {
    if (err) *err = "unexpected reply: " + f[0];
    return false;
  }
  out->commit = f.size() > 1 ? f[1] : "";
  out->candidates.clear();
  out->candidatesEx.clear();
  // After the commit field, candidates arrive as (surface, reading, hinsi)
  // triples. Tolerate a short trailing group (pad missing fields with empty).
  for (size_t i = 2; i < f.size(); i += 3) {
    Candidate c;
    c.surface = f[i];
    c.reading = i + 1 < f.size() ? f[i + 1] : "";
    c.hinsi = i + 2 < f.size() ? f[i + 2] : "";
    if (c.surface.empty()) continue;
    out->candidates.push_back(c.surface);  // keep the flat view populated too
    out->candidatesEx.push_back(std::move(c));
  }
  return true;
}

bool Client::ping(std::string *err) {
  if (fd_ < 0 && !connect(err)) return false;
  if (!send_line(join_tabs({"ping"}), err)) return false;
  std::string line;
  if (!recv_line(&line, err)) return false;
  if (line.rfind("pong", 0) != 0) {
    if (err) *err = "no pong: " + line;
    return false;
  }
  return true;
}

bool Client::simpleReq(const std::string &op, const std::string *arg,
                       const char *expect_verb, std::string *text,
                       std::string *err) {
  if (fd_ < 0 && !connect(err)) return false;
  std::vector<std::string> req{op};
  if (arg) req.push_back(*arg);
  if (!send_line(join_tabs(req), err)) return false;

  std::string line;
  if (!recv_line(&line, err)) return false;
  auto f = split_tabs(line);
  if (f.empty()) {
    if (err) *err = "empty reply";
    return false;
  }
  if (f[0] == "err") {
    if (err) *err = f.size() > 1 ? f[1] : "engine error";
    return false;
  }
  if (f[0] != expect_verb) {
    if (err) *err = "unexpected reply: " + f[0];
    return false;
  }
  if (text) *text = f.size() > 1 ? f[1] : "";
  return true;
}

bool Client::sessionBegin(std::string *preedit, std::string *err) {
  return simpleReq("session-begin", nullptr, "ok", preedit, err);
}

bool Client::sessionKey(const std::string &ascii, std::string *preedit,
                        std::string *err) {
  return simpleReq("key", &ascii, "ok", preedit, err);
}

bool Client::sessionBackspace(std::string *preedit, std::string *err) {
  return simpleReq("backspace", nullptr, "ok", preedit, err);
}

bool Client::sessionConvert(std::string *preedit, std::string *err) {
  return simpleReq("convert-session", nullptr, "ok", preedit, err);
}

bool Client::sessionPreedit(std::string *preedit, std::string *err) {
  return simpleReq("preedit", nullptr, "ok", preedit, err);
}

bool Client::sessionCommit(std::string *committed, std::string *err) {
  return simpleReq("commit", nullptr, "commit", committed, err);
}

bool Client::sessionCancel(std::string *err) {
  return simpleReq("cancel", nullptr, "ok", nullptr, err);
}

bool Client::sessionCandidates(const std::string &romaji, int max,
                               ConvertResult *out, std::string *err) {
  return request("session-candidates", romaji, max, out, err);
}

}  // namespace atzc
