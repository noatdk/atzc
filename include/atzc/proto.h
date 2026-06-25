// Wire protocol shared by the daemon (atzcd), the client library, and the CLI.
//
// One request per line, one reply per line, UTF-8, '\n'-terminated, fields
// separated by '\t'. Japanese text never contains tab or newline, so a flat
// TSV line needs no escaping and is trivial to produce/parse in C++, Python,
// or anything else. No JSON dependency.
//
//   request   convert <romaji> [max]      -> ok <commit> [cand...]   (commit == cand[0])
//             ping                         -> pong
//   reply     err <message>               (on any failure)
//
// `commit` is the engine's top-1 conversion; the candidate list follows. A
// reply with `ok` and no candidates means the engine produced nothing.

#ifndef ATZC_PROTO_H_
#define ATZC_PROTO_H_

#include <string>
#include <vector>

namespace atzc {

// Result of a conversion: top-1 plus the full candidate list (commit == [0]).
struct ConvertResult {
  std::string commit;
  std::vector<std::string> candidates;
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
