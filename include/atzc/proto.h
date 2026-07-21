// Wire protocol shared by the daemon (atzcd), the client library, and the CLI.
//
// One request per line, one reply per line, UTF-8, '\n'-terminated, fields
// separated by '\t'. Japanese text never contains tab or newline, so a flat
// TSV line needs no escaping and is trivial to produce/parse in C++, Python,
// or anything else. No JSON dependency.
//
//   batch     convert <romaji> [max]      -> ok <commit> [cand...]   (commit == cand[0])
//             candidates <romaji> [max]    -> ok <commit> <cand...>
//             candidates-ex <romaji> [max] -> okx <commit> <surf reading hinsi>...
//             ping                         -> pong
//   session   session-begin / key <ascii> / backspace / convert-session /
//             preedit / cancel             -> ok [<preedit>]
//             commit                       -> commit <text>
//             session-candidates <r> [max] -> ok <commit> [cand...]
//   reply     err <message>               (on any failure)
//
// `commit` is the engine's top-1 conversion; the candidate list follows. A
// reply with `ok` and no candidates means the engine produced nothing. The
// session family is a stateful composition; see docs/protocol.md.
//
// candidates-ex returns the ENRICHED list (candidatesEx): each candidate is a
// (surface, reading-hiragana, part-of-speech) triple. It is cleaner and wider
// than the flat list; `candidates` still works unchanged for flat-surface
// clients. See docs/protocol.md.

#ifndef ATZC_PROTO_H_
#define ATZC_PROTO_H_

#include <string>
#include <vector>

namespace atzc {

// One enriched candidate: the surface form plus its reading (hiragana) and
// part-of-speech (hinsi — a label like 名詞, or a hex fallback like 0x08).
struct Candidate {
  std::string surface;
  std::string reading;
  std::string hinsi;
};

// Result of a conversion: top-1, the flat candidate surface list (commit ==
// candidates[0]), and — when the engine supplied it — the enriched list. The
// enriched list is cleaner (no user-dict pollution) and wider (covers readings
// the flat reconversion list misses); candidatesEx[i].surface need not line up
// with candidates[i]. candidatesEx is empty when the engine gave no enriched
// data, in which case consumers fall back to the flat `candidates`.
struct ConvertResult {
  std::string commit;
  std::vector<std::string> candidates;
  std::vector<Candidate> candidatesEx;
};

// Split a line on '\t'. Trailing '\r'/'\n' are ignored by the caller.
inline std::vector<std::string> split_tabs(const std::string &line) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : line) {
    if (c == '\t') {
      out.push_back(cur);
      cur.clear();
    } else if (c != '\r' && c != '\n') {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

// Join fields with '\t' and append '\n'.
inline std::string join_tabs(const std::vector<std::string> &fields) {
  std::string out;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) out.push_back('\t');
    out += fields[i];
  }
  out.push_back('\n');
  return out;
}

}  // namespace atzc

#endif  // ATZC_PROTO_H_
