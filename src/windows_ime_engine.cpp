// WindowsImeEngine — see windows_ime_engine.h.
//
// !!! UNTESTED ON WINDOWS !!!  Written against the proven Wine-harness TSF
// sequence, but it has only ever been read-checked on macOS — it must be built
// and run on a Windows host with the IME installed and iterated there. The
// activation handshake (focus document, profile activation) is the part most
// likely to need adjustment against the real TIP.

#ifdef _WIN32

#include "atzc/windows_ime_engine.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>  // SysAllocString / SysFreeString / SysStringLen
#include <msctf.h>
#include <ctffunc.h>  // ITfFnSearchCandidateProvider, ITfCandidateList, ITfCandidateString

#include <string>
#include <utility>
#include <vector>

namespace atzc {
namespace {

// GUIDs defined locally so this TU needs no uuid import lib (matching how the
// Wine harness carries its own constants). Values: standard TSF GUIDs plus the
// IME's TSF TIP CLSID and its ja-JP language-profile GUID.
const CLSID kClsidThreadMgr = {
    0x529A9E6B, 0x6587, 0x4F23, {0xAB, 0x9E, 0x9C, 0x7D, 0x68, 0x3E, 0x3C, 0x50}};
const IID kIidThreadMgr = {
    0xAA80E801, 0x2021, 0x11D2, {0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E}};
const CLSID kClsidInputProcessorProfiles = {
    0x33C53A50, 0xF456, 0x4884, {0xB0, 0x49, 0x85, 0xFD, 0x64, 0x3E, 0xCF, 0xED}};
const IID kIidInputProcessorProfiles = {
    0x1F02B6C5, 0x7842, 0x4EE6, {0x8A, 0x0B, 0x9A, 0x24, 0x18, 0x3A, 0x95, 0xCA}};
const IID kIidFnSearchCandidateProvider = {
    0x87A2AD8F, 0xF27B, 0x4920, {0x85, 0x01, 0x67, 0x60, 0x22, 0x80, 0x17, 0x5D}};
const GUID kGuidNull = {
    0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
const CLSID kClsidTip = {
    0x1314EB53, 0xCACA, 0x4152, {0xA5, 0x56, 0xA1, 0x84, 0x14, 0x32, 0x02, 0xAF}};
const GUID kImeProfile = {
    0xA38F2FD9, 0x7199, 0x45E1, {0x84, 0x1C, 0xBE, 0x03, 0x13, 0xD8, 0x05, 0x2F}};
constexpr LANGID kLangJa = 0x0411;

std::string wide_to_utf8(const wchar_t *s, int len) {
  if (!s || len <= 0) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring utf8_to_wide(const std::string &s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                              nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(),
                      n);
  return out;
}

template <class T>
void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

class WindowsImeEngine final : public Engine {
 public:
  ~WindowsImeEngine() override { stop(); }

  bool start(std::string *err) override {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    co_init_ = SUCCEEDED(hr);  // S_FALSE = already initialized on this thread

    hr = CoCreateInstance(kClsidThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                          kIidThreadMgr, reinterpret_cast<void **>(&tm_));
    if (FAILED(hr) || !tm_) return fail(err, "CoCreateInstance(TF_ThreadMgr)", hr);
    hr = tm_->Activate(&client_id_);
    if (FAILED(hr)) return fail(err, "ITfThreadMgr::Activate", hr);

    hr = CoCreateInstance(kClsidInputProcessorProfiles, nullptr,
                          CLSCTX_INPROC_SERVER, kIidInputProcessorProfiles,
                          reinterpret_cast<void **>(&profiles_));
    if (FAILED(hr) || !profiles_)
      return fail(err, "CoCreateInstance(InputProcessorProfiles)", hr);
    profiles_->ChangeCurrentLanguage(kLangJa);
    hr = profiles_->ActivateLanguageProfile(kClsidTip, kLangJa, kImeProfile);
    if (FAILED(hr)) return fail(err, "ActivateLanguageProfile", hr);

    return createFocusDocument(err);
  }

  bool topOne(const std::string &romaji, ConvertResult *out,
              std::string *err) override {
    std::vector<std::string> cands;
    if (!searchCandidates(romaji, /*max=*/1, &cands, err)) return false;
    out->commit = cands.empty() ? std::string() : cands.front();
    out->candidates.clear();
    return true;
  }

  bool convert(const std::string &romaji, int max, ConvertResult *out,
               std::string *err) override {
    if (!searchCandidates(romaji, max, &out->candidates, err)) return false;
    out->commit = out->candidates.empty() ? std::string() : out->candidates.front();
    return true;
  }

  void stop() override {
    release(context_);
    if (docmgr_) {
      docmgr_->Pop(TF_POPF_ALL);
      release(docmgr_);
    }
    release(profiles_);
    if (tm_) {
      tm_->Deactivate();
      release(tm_);
    }
    if (hwnd_) {
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
    if (co_init_) {
      CoUninitialize();
      co_init_ = false;
    }
  }

 private:
  bool fail(std::string *err, const char *what, HRESULT hr) {
    if (err) {
      char b[160];
      wsprintfA(b, "atzc: %s failed (hr=0x%08lX)", what, static_cast<unsigned long>(hr));
      *err = b;
    }
    stop();
    return false;
  }

  // A message-only window owns the TSF focus; an empty context (no text store)
  // is enough to activate the TIP for the function-provider / search path, which
  // doesn't read the document. (If the real TIP refuses to serve without a text
  // store, this is where to add a minimal ITextStoreACP — see the Wine harness.)
  bool createFocusDocument(std::string *err) {
    hwnd_ = CreateWindowExW(0, L"STATIC", L"atzc-ime", 0, 0, 0, 0, 0, HWND_MESSAGE,
                            nullptr, nullptr, nullptr);
    HRESULT hr = tm_->CreateDocumentMgr(&docmgr_);
    if (FAILED(hr) || !docmgr_) return fail(err, "CreateDocumentMgr", hr);
    TfEditCookie ec = 0;
    hr = docmgr_->CreateContext(client_id_, 0, nullptr, &context_, &ec);
    if (FAILED(hr) || !context_) return fail(err, "CreateContext", hr);
    docmgr_->Push(context_);
    tm_->SetFocus(docmgr_);
    return true;
  }

  bool searchCandidates(const std::string &romaji, int max,
                        std::vector<std::string> *out, std::string *err) {
    out->clear();
    ITfFunctionProvider *prov = nullptr;
    HRESULT hr = tm_->GetFunctionProvider(kClsidTip, &prov);
    if (FAILED(hr) || !prov) return fail(err, "GetFunctionProvider", hr);

    ITfFnSearchCandidateProvider *scp = nullptr;
    hr = prov->GetFunction(kGuidNull, kIidFnSearchCandidateProvider,
                           reinterpret_cast<IUnknown **>(&scp));
    prov->Release();
    if (FAILED(hr) || !scp)
      return fail(err, "GetFunction(SearchCandidateProvider)", hr);

    std::wstring wq = utf8_to_wide(romaji);
    BSTR query = SysAllocString(wq.c_str());
    BSTR appid = SysAllocString(L"");
    ITfCandidateList *cands = nullptr;
    hr = scp->GetSearchCandidates(query, appid, &cands);
    SysFreeString(query);
    SysFreeString(appid);
    scp->Release();
    if (FAILED(hr) || !cands) return fail(err, "GetSearchCandidates", hr);

    ULONG num = 0;
    cands->GetCandidateNum(&num);
    for (ULONG i = 0; i < num; ++i) {
      if (max > 0 && static_cast<int>(out->size()) >= max) break;
      ITfCandidateString *cs = nullptr;
      if (cands->GetCandidate(i, &cs) == S_OK && cs) {
        BSTR s = nullptr;
        if (cs->GetString(&s) == S_OK && s) {
          out->push_back(wide_to_utf8(s, static_cast<int>(SysStringLen(s))));
          SysFreeString(s);
        }
        // GetCandidate returns the list's shared ITfCandidateString singleton —
        // do NOT Release per iteration (per the Wine harness's reconversion note).
      }
    }
    cands->Release();
    return true;
  }

  bool co_init_ = false;
  TfClientId client_id_ = 0;
  ITfThreadMgr *tm_ = nullptr;
  ITfInputProcessorProfiles *profiles_ = nullptr;
  ITfDocumentMgr *docmgr_ = nullptr;
  ITfContext *context_ = nullptr;
  HWND hwnd_ = nullptr;
};

}  // namespace

std::unique_ptr<Engine> MakeWindowsImeEngine() {
  return std::make_unique<WindowsImeEngine>();
}

}  // namespace atzc

#endif  // _WIN32
