// MakeConverter for POSIX: a reconnecting client to atzcd.
//
// Wraps the raw socket Client with lazy-connect + one reconnect on failure —
// the recovery logic each host used to carry inline. Hosts now just hold the
// returned Converter and call convert()/candidates(); the transport and its
// recovery live here. Not thread-safe; serialize externally (the daemon and
// engine are serial anyway).

#include "atzc/client.h"
#include "atzc/converter.h"

#include <utility>

namespace atzc {
namespace {

class ReconnectingClient : public Converter {
 public:
  explicit ReconnectingClient(std::string socket_path)
      : client_(std::move(socket_path)) {}

  bool convert(const std::string &romaji, ConvertResult *out,
               std::string *err) override {
    return withReconnect(
        [&](std::string *e) { return client_.convert(romaji, out, e); }, err);
  }

  bool candidates(const std::string &romaji, int max, ConvertResult *out,
                  std::string *err) override {
    return withReconnect(
        [&](std::string *e) { return client_.candidates(romaji, max, out, e); },
        err);
  }

 private:
  // Run `call` against the connection; on failure close, reconnect once, retry.
  template <class Fn>
  bool withReconnect(Fn &&call, std::string *err) {
    std::string e;
    if (!client_.connected()) client_.connect(&e);
    if (call(&e)) return true;
    client_.close();
    if (client_.connect(&e) && call(&e)) return true;
    if (err) *err = e.empty() ? "atzc: conversion failed" : e;
    return false;
  }

  Client client_;
};

}  // namespace

std::unique_ptr<Converter> MakeConverter(const ConverterConfig &config) {
  return std::make_unique<ReconnectingClient>(config.socket_path);
}

}  // namespace atzc
