#include "engine.h"

#include <fcitx-utils/key.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

#include <utility>

namespace atzc {
namespace {

constexpr int kPageSize = 9;

// A candidate that commits its string through the engine on selection.
class AtzcCandidate : public fcitx::CandidateWord {
 public:
  AtzcCandidate(AtzcEngine *engine, std::string value)
      : engine_(engine), value_(std::move(value)) {
    setText(fcitx::Text(value_));
  }
  void select(fcitx::InputContext *ic) const override {
    engine_->commit(ic, value_);
  }

 private:
  AtzcEngine *engine_;
  std::string value_;
};

}  // namespace

AtzcEngine::AtzcEngine(fcitx::Instance *instance) : instance_(instance) {
  instance_->inputContextManager().registerProperty("atState", &factory_);
}

void AtzcEngine::updatePreedit(fcitx::InputContext *ic, const std::string &s) {
  fcitx::Text preedit(s, fcitx::TextFormatFlag::Underline);
  if (!s.empty()) preedit.setCursor(static_cast<int>(s.size()));
  ic->inputPanel().setClientPreedit(preedit);
  ic->updatePreedit();
}

void AtzcEngine::clearSession(fcitx::InputContext *ic) {
  state(ic)->clear();
  ic->inputPanel().reset();
  ic->updatePreedit();
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void AtzcEngine::commit(fcitx::InputContext *ic, const std::string &text) {
  ic->commitString(text);
  clearSession(ic);
}

void AtzcEngine::startConversion(fcitx::InputContext *ic) {
  auto *st = state(ic);
  if (st->romaji.empty()) return;

  ConvertResult r;
  std::string err;
  // Opening the candidate window needs the full list; candidates() is usually
  // served from the daemon's prefetch cache. (A future enhancement: call
  // convert() per keystroke for an inline top-1, which also primes that prefetch.)
  if (!client_.connected()) client_.connect(&err);
  if (!client_.candidates(st->romaji, 0, &r, &err)) {
    // One reconnect attempt — atzcd may have restarted.
    client_.close();
    if (!client_.connect(&err) || !client_.candidates(st->romaji, 0, &r, &err)) {
      return;  // leave the romaji preedit as-is so the user can retry
    }
  }
  if (r.candidates.empty()) {
    if (!r.commit.empty()) commit(ic, r.commit);
    return;
  }

  auto list = std::make_unique<fcitx::CommonCandidateList>();
  list->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);
  list->setPageSize(kPageSize);
  list->setSelectionKey(fcitx::Key::keyListFromString("1 2 3 4 5 6 7 8 9"));
  for (const auto &c : r.candidates)
    list->append(std::make_unique<AtzcCandidate>(this, c));
  list->setGlobalCursorIndex(0);
  ic->inputPanel().setCandidateList(std::move(list));
  // Show the top candidate as the preedit while choosing.
  updatePreedit(ic, r.commit.empty() ? r.candidates.front() : r.commit);
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void AtzcEngine::keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &ev) {
  if (ev.isRelease()) return;
  auto *ic = ev.inputContext();
  auto *st = state(ic);
  const fcitx::Key key = ev.key();
  auto candList = ic->inputPanel().candidateList();
  const bool choosing = candList && candList->size() > 0;

  if (choosing) {
    auto *common = dynamic_cast<fcitx::CommonCandidateList *>(candList.get());
    // Number keys select directly.
    if (int idx = key.keyListIndex(
            fcitx::Key::keyListFromString("1 2 3 4 5 6 7 8 9"));
        idx >= 0 && common && idx < common->size()) {
      common->candidate(idx).select(ic);
      return ev.filterAndAccept();
    }
    if (key.check(FcitxKey_space) || key.check(FcitxKey_Down) ||
        key.check(FcitxKey_Tab)) {
      if (common) {
        common->toCursorMovable()->nextCandidate();
        if (common->cursorIndex() >= 0)
          updatePreedit(ic, common->candidate(common->cursorIndex())
                                .text().toString());
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      }
      return ev.filterAndAccept();
    }
    if (key.check(FcitxKey_Up)) {
      if (common) {
        common->toCursorMovable()->prevCandidate();
        if (common->cursorIndex() >= 0)
          updatePreedit(ic, common->candidate(common->cursorIndex())
                                .text().toString());
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      }
      return ev.filterAndAccept();
    }
    if (key.check(FcitxKey_Return) && common && common->cursorIndex() >= 0) {
      common->candidate(common->cursorIndex()).select(ic);
      return ev.filterAndAccept();
    }
    if (key.check(FcitxKey_Escape)) {
      // Drop the candidate list, keep the romaji for editing.
      ic->inputPanel().setCandidateList(nullptr);
      updatePreedit(ic, st->romaji);
      ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      return ev.filterAndAccept();
    }
  }

  // Building the romaji buffer.
  if (key.check(FcitxKey_space) && !st->romaji.empty()) {
    startConversion(ic);
    return ev.filterAndAccept();
  }
  if (key.check(FcitxKey_BackSpace) && !st->romaji.empty()) {
    st->romaji.pop_back();
    ic->inputPanel().setCandidateList(nullptr);
    updatePreedit(ic, st->romaji);
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    return ev.filterAndAccept();
  }
  if (key.check(FcitxKey_Return) && !st->romaji.empty()) {
    commit(ic, st->romaji);  // commit raw romaji if the user never converted
    return ev.filterAndAccept();
  }
  if (key.check(FcitxKey_Escape) && !st->romaji.empty()) {
    clearSession(ic);
    return ev.filterAndAccept();
  }
  // a-z (no modifiers) extends the buffer.
  if (key.isSimple()) {
    auto sym = key.sym();
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
      st->romaji.push_back(static_cast<char>(sym));
      ic->inputPanel().setCandidateList(nullptr);
      updatePreedit(ic, st->romaji);
      ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      return ev.filterAndAccept();
    }
  }
  // Anything else passes through (only meaningful while idle).
}

void AtzcEngine::reset(const fcitx::InputMethodEntry &,
                     fcitx::InputContextEvent &event) {
  clearSession(event.inputContext());
}

}  // namespace atzc

FCITX_ADDON_FACTORY(atzc::AtzcEngineFactory)
