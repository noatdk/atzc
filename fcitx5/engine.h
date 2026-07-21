// fcitx5 input-method addon for the AT engine. Drives a STATEFUL composition on
// atzcd: keystrokes/edits/henkan/commit go over the session protocol (the
// keepalive harness holds the live composition), the returned "ATD PRE" text is
// shown as an underlined preedit, and "ATD COMMIT" text is committed. The
// candidate window uses the single-shot full-list call. The engine is a thin
// front-end: all conversion lives in atzcd.
//
// Build/test on a desktop with fcitx5 dev packages — see ../README.md. (Untested
// at runtime: no fcitx5 desktop on the headless box; it is compile-verified.)

#ifndef ATZC_FCITX5_ENGINE_H_
#define ATZC_FCITX5_ENGINE_H_

#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include <memory>
#include <string>

#include "atzc/client.h"

namespace atzc {

// Per-input-context state. The authoritative composition lives in atzcd (one
// stateful session); here we mirror the current reading/preedit and whether a
// composition is open on the daemon, plus the raw romaji so the candidate-window
// (single-shot) call has a reading to convert.
struct AtzcState : public fcitx::InputContextProperty {
  std::string romaji;   // raw ascii keystrokes typed so far (drives candidates())
  std::string preedit;  // latest "ATD PRE" text from the daemon (what's shown)
  bool composing = false;  // a session composition is open on the daemon
  void clear() {
    romaji.clear();
    preedit.clear();
    composing = false;
  }
};

class AtzcEngine : public fcitx::InputMethodEngineV2 {
 public:
  explicit AtzcEngine(fcitx::Instance *instance);

  void keyEvent(const fcitx::InputMethodEntry &entry,
                fcitx::KeyEvent &keyEvent) override;
  void reset(const fcitx::InputMethodEntry &entry,
             fcitx::InputContextEvent &event) override;

  // Commit a chosen candidate string and clear the session.
  void commit(fcitx::InputContext *ic, const std::string &text);

 private:
  AtzcState *state(fcitx::InputContext *ic) { return ic->propertyFor(&factory_); }
  void updatePreedit(fcitx::InputContext *ic, const std::string &s);
  void clearSession(fcitx::InputContext *ic);
  // Show the full candidate list for the current reading (single-shot call).
  void startConversion(fcitx::InputContext *ic);
  // Ensure a daemon composition is open; false if the daemon is unreachable.
  bool ensureComposing(AtzcState *st, std::string *err);

  fcitx::Instance *instance_;
  fcitx::FactoryFor<AtzcState> factory_{
      [](fcitx::InputContext &) { return new AtzcState; }};
  Client client_;  // connection to atzcd (lazy connect / reconnect)
};

class AtzcEngineFactory : public fcitx::AddonFactory {
 public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new AtzcEngine(manager->instance());
  }
};

}  // namespace atzc

#endif  // ATZC_FCITX5_ENGINE_H_
