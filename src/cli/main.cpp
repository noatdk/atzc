// atzc — exercise the daemon from the shell.
//
//   atzc convert <romaji>            fast top-1
//   atzc candidates <romaji> [max]   full candidate list
//   atzc ping
//   atzc [--socket <path>] ...

#include <cstdio>
#include <cstring>
#include <string>

#include "atzc/client.h"

int main(int argc, char **argv) {
  std::string socket_path;
  int i = 1;
  if (i < argc && std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
    socket_path = argv[i + 1];
    i += 2;
  }
  if (i >= argc) {
    std::fprintf(stderr,
                 "usage: atzc [--socket P] convert <romaji> | "
                 "candidates <romaji> [max] | ping\n");
    return 2;
  }

  atzc::Client client(socket_path);
  std::string err;
  if (!client.connect(&err)) {
    std::fprintf(stderr, "atzc: %s\n", err.c_str());
    return 1;
  }

  std::string op = argv[i++];
  if (op == "ping") {
    if (!client.ping(&err)) {
      std::fprintf(stderr, "atzc: %s\n", err.c_str());
      return 1;
    }
    std::puts("pong");
    return 0;
  }
  if (op == "convert" || op == "candidates") {
    if (i >= argc) {
      std::fprintf(stderr, "atzc: %s needs <romaji>\n", op.c_str());
      return 2;
    }
    std::string romaji = argv[i++];
    atzc::ConvertResult r;
    bool ok;
    if (op == "convert") {
      ok = client.convert(romaji, &r, &err);
    } else {
      int max = i < argc ? std::atoi(argv[i]) : 0;
      ok = client.candidates(romaji, max, &r, &err);
    }
    if (!ok) {
      std::fprintf(stderr, "atzc: %s\n", err.c_str());
      return 1;
    }
    std::printf("commit\t%s\n", r.commit.c_str());
    for (size_t k = 0; k < r.candidates.size(); ++k)
      std::printf("cand[%zu]\t%s\n", k, r.candidates[k].c_str());
    return 0;
  }
  std::fprintf(stderr, "atzc: unknown op '%s'\n", op.c_str());
  return 2;
}
