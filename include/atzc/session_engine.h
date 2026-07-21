// SessionEngine — a stateful composition over one long-lived harness child.
//
// Where HarnessEngine (the batch path) runs a fresh single-shot conversion per
// request, SessionEngine keeps ONE keepalive harness process alive across a
// whole composition and drives it with the session line commands, tracking the
// live preedit as the user types / edits / converts / commits:
//
//   -> sreset / scancel   start / abandon a composition   <- ATD PRE  (empty)
//   -> stype <ascii>      append romaji, settle           <- ATD PRE  <kana>
//   -> sback              delete one kana                 <- ATD PRE  <kana>
//   -> sconv              henkan in place (stays active)  <- ATD PRE  <kanji>
//   -> spre               read the current preedit        <- ATD PRE  <text>
//   -> scommit            finalize                        <- ATD COMMIT <text>
//
// The keepalive child is launched with the warm-reuse / fast-path env family and
// prints "ATD READY" once activated (see HarnessProcess). The worker is
// transparently respawned if the pipe ever dies (mirrors HarnessEngine); a
// respawn implicitly abandons the in-flight composition, so the caller is told
// via a false return and can restart it with begin().
//
// The heavy full-candidate-list path is deliberately NOT here: it stays on the
// single-shot HarnessEngine/Session mechanism (the reconversion teardown
// constraint). SessionEngine owns typing / henkan / commit only.
//
// The protocol is platform-independent; launching and piping the child is not,
// so that lives behind HarnessProcess (POSIX today, native Windows later).

#ifndef ATZC_SESSION_ENGINE_H_
#define ATZC_SESSION_ENGINE_H_

#include <memory>
#include <string>

#include "atzc/harness_process.h"

namespace atzc {

class SessionEngine {
 public:
  // Takes ownership of a harness child launched in keepalive/session mode (e.g.
  // from MakeSessionHarnessProcess). Construction is cheap; start() does the
  // one-time heavy bring-up and warms the first session worker.
  explicit SessionEngine(std::unique_ptr<HarnessProcess> process);
  ~SessionEngine();

  SessionEngine(const SessionEngine &) = delete;
  SessionEngine &operator=(const SessionEngine &) = delete;

  // Bring the harness up and warm one session worker. Slow on a cold prefix.
  bool start(std::string *err);

  // Start (or restart) a composition: ESC + clear. *preedit is emptied.
  bool begin(std::string *preedit, std::string *err);
  // Append romaji keystrokes to the live composition and settle. *preedit is the
  // resulting kana reading.
  bool type(const std::string &ascii, std::string *preedit, std::string *err);
  // Delete one kana from the live composition. *preedit is the reading after.
  bool backspace(std::string *preedit, std::string *err);
  // Henkan in place (composition stays active). *preedit is the kanji surface.
  bool convert(std::string *preedit, std::string *err);
  // Read the current preedit without changing it.
  bool preedit(std::string *out, std::string *err);
  // Finalize the composition. *committed is the committed text; the composition
  // ends (a following begin() starts a fresh one).
  bool commit(std::string *committed, std::string *err);
  // Abandon the composition (ESC + clear); like begin() but semantically "throw
  // away", used when the user cancels.
  bool cancel(std::string *err);

  void stop();

 private:
  // (Re)spawn the session worker and block until it reports "ATD READY".
  bool ensureWarm(std::string *err);
  // Send one command line; read one "ATD PRE"/"ATD COMMIT" reply. `text` (if
  // non-null) receives the reply's payload. Respawns on a dead pipe: a respawn
  // loses the composition, so the second attempt is only made for a still-empty
  // (pre-begin) worker — otherwise the caller is told to re-begin.
  bool runCmd(const std::string &line, bool want_commit, std::string *text,
              std::string *err);
  bool writeCmd(const std::string &line, std::string *err);
  // Read harness stdout for one reply line. want_commit selects the marker:
  // "ATD COMMIT <text>" when true, else "ATD PRE <text>". Returns false on EOF.
  bool readReply(bool want_commit, std::string *text, std::string *err);

  std::unique_ptr<HarnessProcess> proc_;
  std::string rbuf_;   // unconsumed harness stdout bytes (line-buffered parse)
  bool active_ = false;  // a composition has been begun on the current worker
};

// The platform's session harness child: same launcher as the batch harness but
// in keepalive/session mode (long-lived, speaks the s* commands). Selected at
// compile time (POSIX today). `engine_dir` is UTF-8.
std::unique_ptr<HarnessProcess> MakeSessionHarnessProcess(std::string engine_dir);

}  // namespace atzc

#endif  // ATZC_SESSION_ENGINE_H_
