// atzc — exercise the daemon from the shell.
//
//   atzc convert <romaji>            fast top-1
//   atzc candidates <romaji> [max]   full candidate list
//   atzc ping
//   atzc session                     interactive stateful-composition REPL
//   atzc [--socket <path>] ...

#include <cstdio>
#include <cstring>
#include <string>

#include "atzc/client.h"

namespace {

// Interactive REPL over one stateful session: line commands map to the client's
// session API so a human can type/edit/convert/commit by hand. One connection
// is held for the whole session (the daemon composition is stateful).
//
//   b | begin          start/reset the composition
//   k <chars> | <chars> append romaji keystrokes
//   bs                 backspace one kana
//   c                  henkan in place
//   p                  read the current preedit
//   commit             finalize
//   cancel             abandon
//   cand <romaji> [n]  full candidate list for a reading
//   q | quit           exit
int run_session(atzc::Client &client) {
  std::fprintf(stderr,
               "atzc session: b=begin  k <chars>=type  bs=backspace  c=convert"
               "  p=preedit  commit  cancel  cand <r> [n]  q=quit\n");
  std::string err, text, line;
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), stdin)) {
    line = buf;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();
    if (line.empty()) continue;

    std::string cmd = line, arg;
    if (auto sp = line.find(' '); sp != std::string::npos) {
      cmd = line.substr(0, sp);
      arg = line.substr(sp + 1);
    }

    bool ok = true;
    text.clear();
    if (cmd == "q" || cmd == "quit") {
      break;
    } else if (cmd == "b" || cmd == "begin") {
      ok = client.sessionBegin(&text, &err);
    } else if (cmd == "k") {
      ok = client.sessionKey(arg, &text, &err);
    } else if (cmd == "bs") {
      ok = client.sessionBackspace(&text, &err);
    } else if (cmd == "c") {
      ok = client.sessionConvert(&text, &err);
    } else if (cmd == "p") {
      ok = client.sessionPreedit(&text, &err);
    } else if (cmd == "commit") {
      ok = client.sessionCommit(&text, &err);
      if (ok) std::printf("committed\t%s\n", text.c_str());
    } else if (cmd == "cancel") {
      ok = client.sessionCancel(&err);
    } else if (cmd == "cand") {
      std::string romaji = arg;
      int max = 0;
      if (auto sp = arg.find(' '); sp != std::string::npos) {
        romaji = arg.substr(0, sp);
        max = std::atoi(arg.c_str() + sp + 1);
      }
      atzc::ConvertResult r;
      ok = client.sessionCandidates(romaji, max, &r, &err);
      if (ok) {
        std::printf("commit\t%s\n", r.commit.c_str());
        for (size_t i = 0; i < r.candidates.size(); ++i)
          std::printf("cand[%zu]\t%s\n", i, r.candidates[i].c_str());
      }
    } else {
      // Bare text with no command word is treated as keystrokes.
      ok = client.sessionKey(line, &text, &err);
    }

    if (!ok) {
      std::fprintf(stderr, "atzc: %s\n", err.c_str());
      continue;
    }
    if (cmd != "commit" && cmd != "cand")
      std::printf("preedit\t%s\n", text.c_str());
    std::fflush(stdout);
  }
  return 0;
}

}  // namespace

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
                 "candidates <romaji> [max] | candidates-ex <romaji> [max] | "
                 "session | ping\n");
    return 2;
  }

  atzc::Client client(socket_path);
  std::string err;
  if (!client.connect(&err)) {
    std::fprintf(stderr, "atzc: %s\n", err.c_str());
    return 1;
  }

  std::string op = argv[i++];
  if (op == "session") {
    return run_session(client);
  }
  if (op == "ping") {
    if (!client.ping(&err)) {
      std::fprintf(stderr, "atzc: %s\n", err.c_str());
      return 1;
    }
    std::puts("pong");
    return 0;
  }
  if (op == "convert" || op == "candidates" || op == "candidates-ex") {
    if (i >= argc) {
      std::fprintf(stderr, "atzc: %s needs <romaji>\n", op.c_str());
      return 2;
    }
    std::string romaji = argv[i++];
    atzc::ConvertResult r;
    bool ok;
    if (op == "convert") {
      ok = client.convert(romaji, &r, &err);
    } else if (op == "candidates") {
      int max = i < argc ? std::atoi(argv[i]) : 0;
      ok = client.candidates(romaji, max, &r, &err);
    } else {
      int max = i < argc ? std::atoi(argv[i]) : 0;
      ok = client.candidatesEx(romaji, max, &r, &err);
    }
    if (!ok) {
      std::fprintf(stderr, "atzc: %s\n", err.c_str());
      return 1;
    }
    std::printf("commit\t%s\n", r.commit.c_str());
    if (op == "candidates-ex") {
      // surface \t reading \t hinsi per line.
      for (size_t k = 0; k < r.candidatesEx.size(); ++k)
        std::printf("cand[%zu]\t%s\t%s\t%s\n", k,
                    r.candidatesEx[k].surface.c_str(),
                    r.candidatesEx[k].reading.c_str(),
                    r.candidatesEx[k].hinsi.c_str());
    } else {
      for (size_t k = 0; k < r.candidates.size(); ++k)
        std::printf("cand[%zu]\t%s\n", k, r.candidates[k].c_str());
    }
    return 0;
  }
  std::fprintf(stderr, "atzc: unknown op '%s'\n", op.c_str());
  return 2;
}
