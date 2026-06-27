// WindowsImeEngine — native-Windows atzc::Engine that drives the *installed*
// IME via TSF (real msctf), with no Wine and no shims. It mirrors the
// search-candidate path proven in the Wine harness
// (GetFunctionProvider → ITfFnSearchCandidateProvider → GetSearchCandidates),
// which returns a candidate list for a reading with no edit session, commit, or
// reconversion teardown.
//
// This is the Windows counterpart of HarnessEngine: on Linux, atzc drives a
// harvested engine under Wine through a subprocess harness; on Windows, the IME
// is a real installed TIP, so atzc drives it in-process. Both implement Engine,
// so Session / LocalConverter / the modore backend sit unchanged on top.

#ifndef ATZC_WINDOWS_IME_ENGINE_H_
#define ATZC_WINDOWS_IME_ENGINE_H_

#ifdef _WIN32

#include <memory>

#include "atzc/engine.h"

namespace atzc {

// Construct (does not activate — call Engine::start()). Windows only.
std::unique_ptr<Engine> MakeWindowsImeEngine();

}  // namespace atzc

#endif  // _WIN32
#endif  // ATZC_WINDOWS_IME_ENGINE_H_
