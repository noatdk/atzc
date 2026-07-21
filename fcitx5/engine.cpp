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
  auto *st = state(ic);
  // Abandon any live composition on the daemon so its state matches ours.
  if (st->composing) {
    std::string err;
    if (!client_.connected()) client_.connect(&err);
    client_.sessionCancel(&err);
  }
  st->clear();
  ic->inputPanel().reset();
  ic->updatePreedit();
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void AtzcEngine::commit(fcitx::InputContext *ic, const std::string &text) {
  auto *st = state(ic);
  // Committing a chosen string bypasses the daemon's own commit; cancel its
  // live composition first so the next composition starts clean.
  if (st->composing) {
    std::string err;
    if (!client_.connected()) client_.connect(&err);
    client_.sessionCancel(&err);
    st->composing = false;
  }
  ic->commitString(text);
  clearSession(ic);
}

// Ensure a daemon composition is open (begin() it if not). One reconnect retry;
// mirrors the reconnect logic the batch path uses.
bool AtzcEngine::ensureComposing(AtzcState *st, std::string *err) {
  if (st->composing) return true;
  if (!client_.connected() && !client_.connect(err)) return false;
  std::string pre;
  if (!client_.sessionBegin(&pre, err)) {
    client_.close();
    if (!client_.connect(err) || !client_.sessionBegin(&pre, err)) return false;
  }
  st->composing = true;
  st->preedit.clear();
  return true;
}

void AtzcEngine::startConversion(fcitx::InputContext *ic) {
  auto *st = state(ic);
  if (st->romaji.empty()) return;

  ConvertResult r;
  std::string err;
  // Opening the candidate window needs the full list; this is the single-shot
  // (heavy) path, kept separate from the live session per the teardown
  // constraint. Usually a prefetch cache hit on the daemon.
  if (!client_.connected()) client_.connect(&err);
  if (!client_.sessionCandidates(st->romaji, 0, &r, &err)) {
    client_.close();  // atzcd may have restarted — one reconnect attempt.
    if (!client_.connect(&err) ||
        !client_.sessionCandidates(st->romaji, 0, &r, &err)) {
      return;  // leave the preedit as-is so the user can retry
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
      // Drop the candidate list, keep the composition for editing.
      ic->inputPanel().setCandidateList(nullptr);
      updatePreedit(ic, st->preedit.empty() ? st->romaji : st->preedit);
      ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
      return ev.filterAndAccept();
    }
  }

  const bool active = !st->romaji.empty() || st->composing;

  // Space: first press henkan-in-place (session convert); if already converted
  // (candidate window not open but we have a reading), open the candidate list.
  if (key.check(FcitxKey_space) && active) {
    std::string err, pre;
    if (client_.connected() || client_.connect(&err)) {
      if (client_.sessionConvert(&pre, &err)) {
        st->preedit = pre;
        updatePreedit(ic, pre.empty() ? st->romaji : pre);
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
        return ev.filterAndAccept();
      }
    }
    // Session convert unavailable: fall back to the full candidate window.
    startConversion(ic);
    return ev.filterAndAccept();
  }

  if (key.check(FcitxKey_BackSpace) && active) {
    if (!st->romaji.empty()) st->romaji.pop_back();
    std::string err, pre;
    if ((client_.connected() || client_.connect(&err)) &&
        client_.sessionBackspace(&pre, &err)) {
      st->preedit = pre;
    } else {
      st->preedit = st->romaji;  // keep local view usable on a daemon miss
    }
    ic->inputPanel().setCandidateList(nullptr);
    if (st->romaji.empty() && st->preedit.empty()) {
      clearSession(ic);
    } else {
      updatePreedit(ic, st->preedit.empty() ? st->romaji : st->preedit);
      ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }
    return ev.filterAndAccept();
  }

  if (key.check(FcitxKey_Return) && active) {
    std::string err, committed;
    if ((client_.connected() || client_.connect(&err)) &&
        client_.sessionCommit(&committed, &err) && !committed.empty()) {
      st->composing = false;
      ic->commitString(committed);
      clearSession(ic);
    } else {
      // Daemon miss: commit whatever we are showing.
      commit(ic, st->preedit.empty() ? st->romaji : st->preedit);
    }
    return ev.filterAndAccept();
  }

  if (key.check(FcitxKey_Escape) && active) {
    clearSession(ic);
    return ev.filterAndAccept();
  }

  // a-z (no modifiers) extends the composition.
  if (key.isSimple()) {
    auto sym = key.sym();
    if (sym >= FcitxKey_a && sym <= FcitxKey_z) {
      std::string err;
      if (!ensureComposing(st, &err)) return;  // daemon down: let the key pass
      std::string pre;
      const std::string ch(1, static_cast<char>(sym));
      if (client_.sessionKey(ch, &pre, &err)) {
        st->preedit = pre;
      } else {
        st->preedit += ch;  // best-effort local echo on a daemon miss
      }
      st->romaji.push_back(static_cast<char>(sym));
      ic->inputPanel().setCandidateList(nullptr);
      updatePreedit(ic, st->preedit.empty() ? st->romaji : st->preedit);
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
