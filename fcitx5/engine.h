// fcitx5 input-method addon for the AT engine. Buffers romaji, converts via the
// atzcd socket (libatzcclient), and shows AT's candidate list. The engine is a thin
// front-end: all conversion lives in atzcd.
//
// Build/test on a desktop with fcitx5 dev packages — see ../README.md.

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

// Per-input-context state: the romaji being typed before conversion.
struct AtzcState : public fcitx::InputContextProperty {
  std::string romaji;
  void clear() { romaji.clear(); }
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
  // Convert the buffered romaji and show AT's candidate list.
  void startConversion(fcitx::InputContext *ic);

  fcitx::Instance *instance_;
  fcitx::FactoryFor<AtzcState> factory_{
      [](fcitx::InputContext &) { return new AtzcState; }};
  Client client_;  // connection to atzcd (lazy connect / reconnect per convert)
};

class AtzcEngineFactory : public fcitx::AddonFactory {
 public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new AtzcEngine(manager->instance());
  }
};

}  // namespace atzc

#endif  // ATZC_FCITX5_ENGINE_H_
