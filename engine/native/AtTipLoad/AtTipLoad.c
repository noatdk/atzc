/*
 * TSF host that drives ATOK conversion (not read-only SHM sniffing).
 * ITextStoreACP + composition sink + ITfKeystrokeMgr; profile activates TIP.
 */
#include <windows.h>
#include <objbase.h>
#include <msctf.h>
#include <imm.h>
#include <stdlib.h>
#include <string.h>
#include "textstore.h"
#include "at_runtime.h"

typedef void (WINAPI *PFN_AtNsShimInstall)(void);
typedef void (WINAPI *PFN_AtNsShimPatchNow)(void);
typedef HRESULT (WINAPI *PFN_TF_CreateThreadMgr)(ITfThreadMgr **);
typedef HRESULT (WINAPI *PFN_TF_CreateInputProcessorProfiles)(ITfInputProcessorProfiles **);
typedef void (WINAPI *PFN_MsctfShim_AppendChar)(WCHAR ch);
typedef void (WINAPI *PFN_MsctfShim_DumpThreadMgrExState)(void);
typedef void (WINAPI *PFN_MsctfShim_Reset)(void);
typedef int  (WINAPI *PFN_MsctfShim_GetCompText)(WCHAR *out, int max);

static const GUID GUID_TFCAT_TIP_KEYBOARD_LOCAL = {
    0x34745c63, 0xb2f0, 0x4784, {0x8b, 0x67, 0x5e, 0x12, 0xc8, 0x70, 0x1a, 0x31}
};
/* non-static: shared with candidates.c via at_runtime.h */
const CLSID CLSID_ATOK_TIP = {
    0x1314EB53, 0xCACA, 0x4152, {0xA5, 0x56, 0xA1, 0x84, 0x14, 0x32, 0x02, 0xAF}
};
static const IID IID_LOCAL_IClassFactory = {
    0x00000001, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};
static const GUID IID_LOCAL_ITfLangBarItemMgr = {
    0xba468c55, 0x9956, 0x4fb1, {0xa5, 0x9d, 0x52, 0xa7, 0xdd, 0x7c, 0xc6, 0xaa}
};
static const GUID IID_LOCAL_ITfThreadMgrEx = {
    0x3e90ade3, 0x7594, 0x4cb0, {0xbb, 0x58, 0x69, 0x62, 0x8f, 0x5f, 0x45, 0x8c}
};
#ifndef TF_TMAE_UIELEMENTENABLEDONLY
#define TF_TMAE_UIELEMENTENABLEDONLY 0x00000008
#endif
static const GUID IID_LOCAL_ITfCategoryMgr = {
    0xc3acefb5, 0xf69d, 0x4905, {0x93, 0x8f, 0xfc, 0xad, 0xcf, 0x4b, 0xe8, 0x30}
};
static const CLSID CLSID_LOCAL_TF_CategoryMgr = {
    0xa4b544a1, 0x438d, 0x4b41, {0x93, 0x25, 0x86, 0x95, 0x23, 0xe2, 0xd6, 0xc7}
};
static const GUID GUID_PROP_TEXTOWNER_LOCAL = {
    0xf1e2d520, 0x0969, 0x11d3, {0x8d, 0xf0, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}
};
static const GUID GUID_PROP_COMPOSING_LOCAL = {
    0xe12ac060, 0xaf15, 0x11d2, {0xaf, 0xc5, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}
};
static const GUID GUID_PROP_LANGID_LOCAL = {
    0x3280ce20, 0x8032, 0x11d2, {0xb6, 0x03, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}
};
static const GUID GUID_PROP_READING_LOCAL = {
    0x5463f7c0, 0x8e31, 0x11d2, {0xbf, 0x46, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}
};
static const GUID GUID_ATOK_PROFILE = {
    0xa38f2fd9, 0x7199, 0x45e1, {0x84, 0x1c, 0xbe, 0x03, 0x13, 0xd8, 0x05, 0x2f}
};

/* TSF reconversion / candidate / function-provider local interface views and
 * the probe functions that use them live in candidates.c now (declared in
 * at_runtime.h). */

#define LANGID_JA 0x411

static PFN_TF_CreateThreadMgr pTF_CreateThreadMgr;
static PFN_TF_CreateInputProcessorProfiles pTF_CreateInputProcessorProfiles;
static PFN_MsctfShim_AppendChar pMsctfShim_AppendChar;
static PFN_MsctfShim_DumpThreadMgrExState pMsctfShim_DumpThreadMgrExState;
static PFN_MsctfShim_Reset pMsctfShim_Reset;
static PFN_MsctfShim_GetCompText pMsctfShim_GetCompText;
/* non-static: shared with candidates.c via at_runtime.h. Runtime-command
 * override for the reconversion select index (set by `reconv <n>`);
 * 0xFFFFFFFF = fall back to the AT_RECONV_SELECT env. */
DWORD g_reconv_select = 0xFFFFFFFF;

static HWND g_hwndFrame;
static ITfKeystrokeMgr *g_km;
ITfThreadMgr *g_tm;   /* non-static: shared with candidates.c */
static ITfDocumentMgr *g_docmgr;
static ITfContext *g_context;
static ITfInputProcessorProfiles *g_profiles;
static ITfTextInputProcessor *g_tip;
static BOOL g_use_keystroke_mgr;
static BOOL g_runtime_mode;
/* Daemon mode (AT_TIPLOAD_DAEMON): a relay server owns this process over
 * stdin/stdout. stdin command lines are marshalled to the main thread (where
 * the TIP lives) and each is acknowledged with an "ATD READY" sentinel; the
 * `convert` command emits a structured "ATD" result block. Verbose logging is
 * kept off stdout (file only) so the stdout stream is a clean protocol. */
static BOOL g_daemon_mode;
static BOOL g_quiet;       /* AT_QUIET: suppress verbose per-callback logging (hot path) */
static BOOL g_runtime_quit;
static BOOL g_runtime_interim_input;
static BOOL g_runtime_snapshot_only;
TfClientId g_client_id;   /* non-static: shared with candidates.c */
static DWORD g_runtime_type_delay_ms;
/* CE name table (g_ce_name[s]/counts) moved to ce_probe.c. */
static HANDLE g_runtime_log_file = INVALID_HANDLE_VALUE;

static void runtime_log_init(void)
{
    if (g_runtime_log_file == INVALID_HANDLE_VALUE) {
        g_runtime_log_file = CreateFileA("tipruntime.log", FILE_APPEND_DATA,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
}

static void log_bytes(const char *s, DWORD len)
{
    DWORD w;
    if (g_quiet) return;   /* AT_QUIET: drop verbose logging off the hot path */
    runtime_log_init();
    /* In daemon mode stdout carries the wire protocol only; the verbose log
     * still goes to tipruntime.log. Otherwise mirror everything to stdout. */
    if (!g_daemon_mode) {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h) WriteFile(h, s, lstrlenA(s), &w, 0);
    }
    if (g_runtime_log_file != INVALID_HANDLE_VALUE) WriteFile(g_runtime_log_file, s, len, &w, 0);
}

/* Emit one protocol line to stdout only (daemon mode). */
static void daemon_out(const char *s)
{
    DWORD w;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h) WriteFile(h, s, lstrlenA(s), &w, 0);
}

/* Phase timing probe (AT_TIPLOAD_TIMING): emit `ATD MS <label> <elapsed_ms>` so
 * we can see exactly where a convert spends its time. Absolute ms from process
 * start; deltas computed offline. The relay ignores any ATD line it doesn't
 * recognize, so this is safe to leave wired behind the env gate. */
static DWORD g_tick0;
static BOOL  g_timing;
static BOOL  g_fast_keys;   /* AT_FAST_KEYS: skip advisory TestKeyDown/Up + per-key log */
static void daemon_ms(const char *label)
{
    char b[96];
    if (!g_timing) return;
    wsprintfA(b, "ATD MS %s %lu\r\n", label, (unsigned long)(GetTickCount() - g_tick0));
    daemon_out(b);
}
/* Non-static shim so candidates.c can stamp phases too. */
void at_daemon_ms(const char *label) { daemon_ms(label); }

/* Emit the captured candidate list as an ATD result block. Called from the
 * reconversion enumerator before the (fault-prone) edit-session teardown so the
 * relay gets the result even if the process then crashes. top-1 == cand[0]. */
void at_daemon_emit_result(void)
{
    char ln[AT_CAP_BYTES + 32];
    int k;
    daemon_out("ATD BEGIN\r\n");
    if (g_daemon_cap_count > 0) {
        wsprintfA(ln, "ATD COMMIT %s\r\n", g_daemon_cap[0]);
        daemon_out(ln);
    }
    for (k = 0; k < g_daemon_cap_count; k++) {
        wsprintfA(ln, "ATD CAND %s\r\n", g_daemon_cap[k]);
        daemon_out(ln);
    }
    daemon_out("ATD END\r\n");
}

void log_str(const char *s)   /* non-static: shared with candidates.c (file-aware logger) */
{
    log_bytes(s, (DWORD)lstrlenA(s));
}

static void log_hr(const char *tag, HRESULT hr)
{
    char buf[96];
    wsprintfA(buf, "%s hr=0x%08lX\r\n", tag, (unsigned long)hr);
    log_str(buf);
}

static int cstrlen_a(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void write_line_a(const char *s)
{
    char line[1024];
    DWORD len = (DWORD)cstrlen_a(s);
    DWORD w;
    if (len >= sizeof(line) - 3) len = (DWORD)sizeof(line) - 3;
    CopyMemory(line, s, len);
    line[len++] = '\r';
    line[len++] = '\n';
    line[len] = 0;
    log_bytes(line, len);
}

static void log_pid(const char *tag)
{
    char buf[96];
    wsprintfA(buf, "%s pid=%lu", tag, (unsigned long)GetCurrentProcessId());
    write_line_a(buf);
}

static void log_ime_status(HWND hwnd, const char *tag)
{
    HIMC himc;
    DWORD conv = 0;
    DWORD sent = 0;
    char buf[160];

    himc = ImmGetContext(hwnd);
    if (!himc) {
        write_line_a("IMM status: no context");
        return;
    }
    if (!ImmGetOpenStatus(himc)) {
        wsprintfA(buf, "%s open=0", tag);
        write_line_a(buf);
        ImmReleaseContext(hwnd, himc);
        return;
    }
    if (!ImmGetConversionStatus(himc, &conv, &sent)) {
        wsprintfA(buf, "%s open=1 conv=<failed>", tag);
        write_line_a(buf);
        ImmReleaseContext(hwnd, himc);
        return;
    }
    wsprintfA(buf, "%s open=1 conv=0x%08lX sent=0x%08lX", tag, (unsigned long)conv, (unsigned long)sent);
    write_line_a(buf);
    ImmReleaseContext(hwnd, himc);
}

static const char *ime_notify_name(WPARAM wp)
{
    switch ((DWORD)wp) {
    case 0x0001: return "IMN_CLOSESTATUSWINDOW";
    case 0x0002: return "IMN_OPENSTATUSWINDOW";
    case 0x0003: return "IMN_CHANGECANDIDATE";
    case 0x0004: return "IMN_CLOSECANDIDATE";
    case 0x0005: return "IMN_OPENCANDIDATE";
    case 0x0006: return "IMN_SETCANDIDATEPOS";
    case 0x0007: return "IMN_SETCOMPOSITIONFONT";
    case 0x0008: return "IMN_SETCOMPOSITIONWINDOW";
    case 0x0009: return "IMN_SETSTATUSWINDOWPOS";
    case 0x000A: return "IMN_GUIDELINE";
    case 0x000B: return "IMN_PRIVATE";
    default: return 0;
    }
}

DWORD get_env_dword(const wchar_t *name, DWORD fallback)   /* non-static: shared with candidates.c */
{
    wchar_t buf[32];
    DWORD len;
    DWORD value = 0;
    const wchar_t *p;

    len = GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (!len || len >= (DWORD)(sizeof(buf) / sizeof(buf[0])))
        return fallback;
    p = buf;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) {
        /* hex form (e.g. 0x77 for VK_F8) */
        p += 2;
        if (!*p) return fallback;
        for (; *p; ++p) {
            DWORD d;
            if (*p >= L'0' && *p <= L'9') d = (DWORD)(*p - L'0');
            else if (*p >= L'a' && *p <= L'f') d = (DWORD)(*p - L'a' + 10);
            else if (*p >= L'A' && *p <= L'F') d = (DWORD)(*p - L'A' + 10);
            else return fallback;
            value = value * 16 + d;
        }
        return value;
    }
    for (; *p; ++p) {
        if (*p < L'0' || *p > L'9')
            return fallback;
        value = value * 10 + (DWORD)(*p - L'0');
    }
    if (p == buf)
        return fallback;
    return value;
}

BOOL env_flag_enabled(const wchar_t *name)   /* non-static: shared with candidates.c */
{
    WCHAR buf[16];

    if (GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]))) == 0)
        return FALSE;
    if (buf[0] == L'0' || buf[0] == L'n' || buf[0] == L'N' || buf[0] == L'f' || buf[0] == L'F')
        return FALSE;
    return TRUE;
}

/* upper_ascii_w / streqi_w moved to ce_probe.c (their only users). */

static void set_compartment_dword(ITfCompartmentMgr *mgr, TfClientId client_id, REFGUID guid, LONG value)
{
    ITfCompartment *comp = 0;
    VARIANT var;

    if (!mgr) return;
    if (mgr->lpVtbl->GetCompartment(mgr, guid, &comp) != S_OK || !comp) return;
    ZeroMemory(&var, sizeof(var));
    V_VT(&var) = VT_I4;
    V_I4(&var) = value;
    comp->lpVtbl->SetValue(comp, client_id, &var);
    comp->lpVtbl->Release(comp);
}

static void log_compartment_value(ITfCompartmentMgr *mgr, TfClientId client_id, const char *label, REFGUID guid)
{
    ITfCompartment *comp = 0;
    VARIANT var;
    char buf[192];
    HRESULT hr;

    if (!mgr) return;

    hr = mgr->lpVtbl->GetCompartment(mgr, guid, &comp);
    wsprintfA(buf, "runtime compartment %s GetCompartment hr=0x%08lX", label, (unsigned long)hr);
    write_line_a(buf);
    if (hr != S_OK || !comp) return;

    ZeroMemory(&var, sizeof(var));
    hr = comp->lpVtbl->GetValue(comp, &var);
    wsprintfA(buf, "runtime compartment %s GetValue hr=0x%08lX vt=0x%04X", label, (unsigned long)hr, (unsigned int)V_VT(&var));
    write_line_a(buf);
    if (hr == S_OK) {
        if (V_VT(&var) == VT_I4) {
            wsprintfA(buf, "runtime compartment %s value=%ld client=%lu", label, (long)V_I4(&var), (unsigned long)client_id);
            write_line_a(buf);
        } else if (V_VT(&var) == VT_BOOL) {
            wsprintfA(buf, "runtime compartment %s value=%d client=%lu", label, V_BOOL(&var) ? 1 : 0, (unsigned long)client_id);
            write_line_a(buf);
        }
    }
    comp->lpVtbl->Release(comp);
}

static void runtime_probe_compartments(void)
{
    struct {
        const char *tag;
        IUnknown *obj;
    } probes[] = {
        { "tm", (IUnknown *)g_tm },
        { "docmgr", (IUnknown *)g_docmgr },
        { "context", (IUnknown *)g_context },
    };
    const struct {
        const char *label;
        REFGUID guid;
    } items[] = {
        { "GUID_PROP_TEXTOWNER", &GUID_PROP_TEXTOWNER_LOCAL },
        { "GUID_PROP_COMPOSING", &GUID_PROP_COMPOSING_LOCAL },
        { "GUID_PROP_LANGID", &GUID_PROP_LANGID_LOCAL },
        { "GUID_PROP_READING", &GUID_PROP_READING_LOCAL },
        { "GUID_COMPARTMENT_KEYBOARD_OPENCLOSE", &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE },
        { "GUID_COMPARTMENT_KEYBOARD_DISABLED", &GUID_COMPARTMENT_KEYBOARD_DISABLED },
        { "GUID_COMPARTMENT_EMPTYCONTEXT", &GUID_COMPARTMENT_EMPTYCONTEXT },
    };
    size_t i, j;

    for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        ITfCompartmentMgr *mgr = 0;
        HRESULT hr;
        char buf[128];
        IEnumGUID *en = 0;

        if (!probes[i].obj) {
            wsprintfA(buf, "runtime compartment %s: no object", probes[i].tag);
            write_line_a(buf);
            continue;
        }

        hr = probes[i].obj->lpVtbl->QueryInterface(probes[i].obj, &IID_ITfCompartmentMgr, (void **)&mgr);
        wsprintfA(buf, "runtime compartment %s QI hr=0x%08lX", probes[i].tag, (unsigned long)hr);
        write_line_a(buf);
        if (hr != S_OK || !mgr) continue;

        hr = mgr->lpVtbl->EnumCompartments(mgr, &en);
        wsprintfA(buf, "runtime compartment %s EnumCompartments hr=0x%08lX", probes[i].tag, (unsigned long)hr);
        write_line_a(buf);
        if (hr == S_OK && en) {
            write_line_a("runtime compartment enum ok");
            en->lpVtbl->Release(en);
        }

        for (j = 0; j < sizeof(items) / sizeof(items[0]); j++) {
            log_compartment_value(mgr, g_client_id, items[j].label, items[j].guid);
        }

        mgr->lpVtbl->Release(mgr);
    }
}

static void runtime_probe_selection(void)
{
    ITfContext *ctx = g_context;
    HRESULT hr;
    char buf[128];

    if (g_docmgr) {
        ITfContext *top = 0;
        hr = g_docmgr->lpVtbl->GetTop(g_docmgr, &top);
        wsprintfA(buf, "runtime selection GetTop hr=0x%08lX ctx=%p", (unsigned long)hr, top);
        write_line_a(buf);
        if (hr == S_OK && top) {
            ctx = top;
        }
    }
    TextStore_LogSelection(ctx, "context");
    if (ctx && ctx != g_context) {
        ctx->lpVtbl->Release(ctx);
    }
}

static void set_keyboard_compartments(ITfThreadMgr *tm, ITfContext *context, TfClientId client_id)
{
    ITfCompartmentMgr *mgr = 0;

    if (tm && tm->lpVtbl->GetGlobalCompartment(tm, &mgr) == S_OK && mgr) {
        set_compartment_dword(mgr, client_id, &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, 1);
        mgr->lpVtbl->Release(mgr);
    }
    if (tm && tm->lpVtbl->QueryInterface(tm, &IID_ITfCompartmentMgr, (void **)&mgr) == S_OK && mgr) {
        set_compartment_dword(mgr, client_id, &GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, 1);
        set_compartment_dword(mgr, client_id, &GUID_COMPARTMENT_KEYBOARD_DISABLED, 0);
        mgr->lpVtbl->Release(mgr);
    }
    if (context && context->lpVtbl->QueryInterface(context, &IID_ITfCompartmentMgr, (void **)&mgr) == S_OK && mgr) {
        set_compartment_dword(mgr, client_id, &GUID_COMPARTMENT_KEYBOARD_DISABLED, 0);
        set_compartment_dword(mgr, client_id, &GUID_COMPARTMENT_EMPTYCONTEXT, 0);
        mgr->lpVtbl->Release(mgr);
    }
}

static void install_ns_shim(void);

static HMODULE g_atok31de_module;

static void preload_atok_engine_dependency(void)
{
    if (g_atok31de_module)
        return;

    SetDllDirectoryW(L"C:\\Program Files\\JustSystems\\ATOK31T29\\");
    g_atok31de_module = LoadLibraryW(L"Atok31De.dll");
    if (!g_atok31de_module)
        log_hr("preload_atok_engine: Atok31De.dll err=", GetLastError());
    else
        write_line_a("preload_atok_engine: Atok31De.dll ok");
}

static void preload_tip_dependencies(void)
{
    SetDllDirectoryW(L"C:\\Program Files\\JustSystems\\ATOK31T29\\");
    write_line_a("preload_tip_deps: begin");
    preload_atok_engine_dependency();
    if (!g_atok31de_module)
        write_line_a("preload_tip_deps: Atok31De.dll unavailable");
    else
        write_line_a("preload_tip_deps: Atok31De.dll already loaded");
    if (!env_flag_enabled(L"AT_SKIP_RT_PRELOAD")) {
        if (!LoadLibraryW(L"ATOK31RT.DLL"))
            log_hr("preload_tip_deps: ATOK31RT.DLL err=", GetLastError());
        else
            write_line_a("preload_tip_deps: ATOK31RT.DLL ok");
    } else {
        write_line_a("preload_tip_deps: ATOK31RT.DLL skipped");
    }
    if (!LoadLibraryW(L"ATOK31TIP.DLL"))
        log_hr("preload_tip_deps: ATOK31TIP.DLL err=", GetLastError());
    else
        write_line_a("preload_tip_deps: ATOK31TIP.DLL ok");
    write_line_a("preload_tip_deps: end");
}

static HRESULT load_atok_tip_instance(ITfTextInputProcessor **ppTip)
{
    HRESULT hr;
    HMODULE mod;
    static const WCHAR *const load_paths[] = {
        L"C:\\Program Files\\JustSystems\\ATOK31T29\\ATOK31TIP.DLL",
        L"ATOK31TIP.DLL",
        NULL
    };
    size_t i;
    typedef HRESULT (WINAPI *PFN_DllGetClassObject)(REFCLSID, REFIID, void **);
    PFN_DllGetClassObject pfn;
    IClassFactory *factory = NULL;
    ITfTextInputProcessor *tip = NULL;

    *ppTip = NULL;

    preload_tip_dependencies();

    /* CoCreateInstance is the safe path under Wine; LoadLibrary can page-fault in DllMain. */
    hr = CoCreateInstance(&CLSID_ATOK_TIP, 0, CLSCTX_INPROC_SERVER,
                          &IID_ITfTextInputProcessor, (void **)&tip);
    log_hr("load_atok_tip: CoCreateInstance", hr);
    if (hr == S_OK && tip) {
        *ppTip = tip;
        SetDllDirectoryW(NULL);
        return S_OK;
    }
    if (tip) {
        tip->lpVtbl->Release(tip);
        tip = NULL;
    }

    preload_atok_engine_dependency();
    if (!g_atok31de_module)
        log_str("load_atok_tip: preload Atok31De.dll unavailable\r\n");
    else
        log_str("load_atok_tip: preload Atok31De.dll ok\r\n");
    if (!LoadLibraryW(L"ATOK31RT.DLL"))
        log_hr("load_atok_tip: preload ATOK31RT.DLL err=", GetLastError());
    else
        log_str("load_atok_tip: preload ATOK31RT.DLL ok\r\n");

    mod = NULL;
    for (i = 0; load_paths[i]; i++) {
        log_str("load_atok_tip: LoadLibraryW begin\r\n");
        mod = LoadLibraryW(load_paths[i]);
        if (mod)
            break;
        log_hr("load_atok_tip: LoadLibraryW err=", GetLastError());
    }
    if (!mod)
        return hr != S_OK ? hr : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    log_hr("load_atok_tip: LoadLibraryW ok", S_OK);
    pfn = (PFN_DllGetClassObject)GetProcAddress(mod, "DllGetClassObject");
    if (!pfn) {
        log_str("load_atok_tip: DllGetClassObject missing\r\n");
        return E_FAIL;
    }
    hr = pfn(&CLSID_ATOK_TIP, &IID_LOCAL_IClassFactory, (void **)&factory);
    log_hr("load_atok_tip: DllGetClassObject", hr);
    if (hr != S_OK || !factory)
        return hr;
    hr = factory->lpVtbl->CreateInstance(factory, NULL, &IID_ITfTextInputProcessor, (void **)&tip);
    log_hr("load_atok_tip: IClassFactory::CreateInstance", hr);
    factory->lpVtbl->Release(factory);
    if (hr == S_OK)
        *ppTip = tip;
    SetDllDirectoryW(NULL);
    return hr;
}

static void install_ns_shim(void)
{
    static BOOL installed;
    HMODULE shim;
    PFN_AtNsShimInstall install;

    if (installed)
        return;

    log_str("AtTipLoad: load AtNsShim.dll\r\n");
    shim = LoadLibraryW(L"AtNsShim.dll");
    if (!shim) {
        log_hr("AtTipLoad: LoadLibraryW(AtNsShim.dll) err=", GetLastError());
        return;
    }
    install = (PFN_AtNsShimInstall)GetProcAddress(shim, "AtNsShimInstall");
    if (!install) {
        install = (PFN_AtNsShimInstall)GetProcAddress(shim, "AtNsShimInstall@0");
    }
    if (!install) {
        log_hr("AtTipLoad: GetProcAddress(AtNsShimInstall) err=", GetLastError());
        return;
    }
    install();
    installed = TRUE;
}

static void patch_ns_shim_now(void)
{
    HMODULE shim;
    PFN_AtNsShimPatchNow patch_now;

    shim = GetModuleHandleW(L"AtNsShim.dll");
    if (!shim)
        shim = LoadLibraryW(L"AtNsShim.dll");
    if (!shim) {
        log_hr("AtTipLoad: LoadLibraryW(AtNsShim.dll) for patch err=", GetLastError());
        return;
    }
    patch_now = (PFN_AtNsShimPatchNow)GetProcAddress(shim, "AtNsShimPatchNow");
    if (!patch_now) {
        patch_now = (PFN_AtNsShimPatchNow)GetProcAddress(shim, "AtNsShimPatchNow@0");
    }
    if (!patch_now) {
        log_hr("AtTipLoad: GetProcAddress(AtNsShimPatchNow) err=", GetLastError());
        return;
    }
    patch_now();
}

static void pump_messages_until_quit(void)
{
    MSG msg;
    while (!g_runtime_quit) {
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

static void resolve_shim_text_bridge(void)
{
    HMODULE mod = GetModuleHandleW(L"msctf.dll");
    char buf[128];
    FARPROC proc = 0;
    if (!mod) return;
    proc = GetProcAddress(mod, "MsctfShim_AppendChar");
    if (!proc) proc = GetProcAddress(mod, "MsctfShim_AppendChar@4");
    if (!proc) proc = GetProcAddress(mod, "_MsctfShim_AppendChar@4");
    pMsctfShim_AppendChar = (PFN_MsctfShim_AppendChar)proc;
    wsprintfA(buf, "runtime bridge MsctfShim_AppendChar=%p", pMsctfShim_AppendChar);
    write_line_a(buf);
    proc = GetProcAddress(mod, "MsctfShim_DumpThreadMgrExState");
    if (!proc) proc = GetProcAddress(mod, "MsctfShim_DumpThreadMgrExState@0");
    if (!proc) proc = GetProcAddress(mod, "_MsctfShim_DumpThreadMgrExState@0");
    pMsctfShim_DumpThreadMgrExState = (PFN_MsctfShim_DumpThreadMgrExState)proc;
    wsprintfA(buf, "runtime bridge MsctfShim_DumpThreadMgrExState=%p", pMsctfShim_DumpThreadMgrExState);
    write_line_a(buf);
    proc = GetProcAddress(mod, "MsctfShim_Reset");
    if (!proc) proc = GetProcAddress(mod, "MsctfShim_Reset@0");
    if (!proc) proc = GetProcAddress(mod, "_MsctfShim_Reset@0");
    pMsctfShim_Reset = (PFN_MsctfShim_Reset)proc;
    wsprintfA(buf, "runtime bridge MsctfShim_Reset=%p", pMsctfShim_Reset);
    write_line_a(buf);
    proc = GetProcAddress(mod, "MsctfShim_GetCompText");
    if (!proc) proc = GetProcAddress(mod, "MsctfShim_GetCompText@8");
    if (!proc) proc = GetProcAddress(mod, "_MsctfShim_GetCompText@8");
    pMsctfShim_GetCompText = (PFN_MsctfShim_GetCompText)proc;
    wsprintfA(buf, "runtime bridge MsctfShim_GetCompText=%p", pMsctfShim_GetCompText);
    write_line_a(buf);
}

static void run_runtime_command(char *cmd);

static DWORD WINAPI runtime_stdin_thread(LPVOID param)
{
    (void)param;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    char buf[256];
    char line[512];
    DWORD got = 0;
    DWORD i;
    int line_len = 0;
    HRESULT co_hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);

    write_line_a("AtTipLoad: stdin runtime loop ready");
    log_hr("AtTipLoad: stdin CoInitializeEx", co_hr);
    /* Tell the relay the engine is up and accepting commands. */
    daemon_ms("ready");
    if (g_daemon_mode) daemon_out("ATD READY\r\n");
    if (!in) {
        write_line_a("AtTipLoad: no stdin handle");
        if (SUCCEEDED(co_hr)) CoUninitialize();
        return 0;
    }

    while (!g_runtime_quit && ReadFile(in, buf, (DWORD)sizeof(buf), &got, 0) && got > 0) {
        for (i = 0; i < got; i++) {
            char ch = buf[i];
            if (ch == '\r' || ch == '\n') {
                if (line_len > 0) {
                    line[line_len] = 0;
                    /* Edit sessions (reconversion) must run on the thread that
                     * created the TIP. Marshal the command to the main thread via
                     * WM_COPYDATA; SendMessage blocks until it has run, so the
                     * READY sentinel below is emitted only after completion. */
                    if (g_daemon_mode && g_hwndFrame) {
                        COPYDATASTRUCT cds;
                        cds.dwData = 0;
                        cds.cbData = (DWORD)line_len + 1;
                        cds.lpData = line;
                        SendMessageW(g_hwndFrame, WM_COPYDATA, 0, (LPARAM)&cds);
                    } else {
                        run_runtime_command(line);
                    }
                    if (g_daemon_mode) daemon_out("ATD READY\r\n");
                    line_len = 0;
                }
                continue;
            }
            if (line_len + 1 < (int)sizeof(line)) {
                line[line_len++] = ch;
            }
        }
    }
    if (SUCCEEDED(co_hr)) CoUninitialize();
    return 0;
}

static void send_key(ITfKeystrokeMgr *km, WPARAM vk, BOOL allow_fallback);
static void log_ime_composition(HWND hwnd);
static void runtime_probe_ime(HWND hwnd, const char *tag);
static void runtime_probe_categorymgr(void);
static void ime_open(HWND hwnd);
static void drive_conversion(ITfKeystrokeMgr *km, ITfContext *context);

static int load_msctf(void)
{
    HMODULE m = LoadLibraryW(L"msctf.dll");
    if (!m) return 0;
    pTF_CreateThreadMgr = (PFN_TF_CreateThreadMgr)GetProcAddress(m, "TF_CreateThreadMgr");
    if (!pTF_CreateThreadMgr) {
        pTF_CreateThreadMgr = (PFN_TF_CreateThreadMgr)GetProcAddress(m, "TF_CreateThreadMgr@4");
    }
    pTF_CreateInputProcessorProfiles = (PFN_TF_CreateInputProcessorProfiles)GetProcAddress(
        m, "TF_CreateInputProcessorProfiles");
    if (!pTF_CreateInputProcessorProfiles) {
        pTF_CreateInputProcessorProfiles = (PFN_TF_CreateInputProcessorProfiles)GetProcAddress(
            m, "TF_CreateInputProcessorProfiles@4");
    }
    return pTF_CreateThreadMgr != 0 && pTF_CreateInputProcessorProfiles != 0;
}

static void pump_messages(int ms)
{
    MSG msg;
    DWORD until = GetTickCount() + (DWORD)ms;
    /* Always drain the queue at least once — keystrokes are delivered
     * synchronously through the KeystrokeMgr, so a single dispatch is enough for
     * any messages ATOK posts; the timed Sleep loop only adds settle time when
     * ms>0. This lets the convert path run with AT_PUMP_MS=0 (no wasted sleep)
     * while still dispatching pending messages. */
    do {
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetTickCount() >= until) break;
        Sleep(10);
    } while (GetTickCount() < until);
}

static DWORD get_type_delay_ms(void)
{
    if (g_runtime_type_delay_ms) return g_runtime_type_delay_ms;
    return get_env_dword(L"AT_TYPE_DELAY_MS", 180);
}

static void force_focus(HWND hwnd)
{
    DWORD fg_tid = GetWindowThreadProcessId(GetForegroundWindow(), 0);
    DWORD our_tid = GetCurrentThreadId();
    AllowSetForegroundWindow(ASFW_ANY);
    if (fg_tid && fg_tid != our_tid)
        AttachThreadInput(our_tid, fg_tid, TRUE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    if (fg_tid && fg_tid != our_tid)
        AttachThreadInput(our_tid, fg_tid, FALSE);
}

static void send_ascii_text_a(ITfKeystrokeMgr *km, const char *text)
{
    DWORD type_delay_ms = get_type_delay_ms();
    int i;
    for (i = 0; text && text[i]; i++) {
        char ch = text[i];
        WPARAM vk = 0;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (ch >= 'A' && ch <= 'Z') {
            vk = (WPARAM)ch;
        } else if (ch == ' ') {
            vk = VK_SPACE;
        } else if (ch == '\n' || ch == '\r') {
            vk = VK_RETURN;
        }
        if (!vk) continue;
        send_key(km, vk, TRUE);
        Sleep(type_delay_ms);
    }
}

static int starts_with_a(const char *s, const char *prefix)
{
    while (*prefix) {
        char a = *s++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        if (!a) return 1;
    }
    return 1;
}

static int streq_icase_a(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static void runtime_dump(const char *tag)
{
    TextStore_LogContent();
    log_ime_status(g_hwndFrame, tag);
    log_ime_composition(g_hwndFrame);
    if (g_context) {
        Composition_PollContext(g_context, tag);
    }
    log_ce_snapshot(tag);
}

static const char *runtime_dump_tmex_state(void)
{
    if (pMsctfShim_DumpThreadMgrExState) {
        pMsctfShim_DumpThreadMgrExState();
        return "direct";
    }
    if (pMsctfShim_AppendChar) {
        pMsctfShim_AppendChar((WCHAR)0xFFFF);
        return "sentinel";
    }
    write_line_a("runtime tmex: no state dumper");
    return "none";
}

static void runtime_help(void)
{
    write_line_a("runtime commands:");
    write_line_a("  help");
    write_line_a("  focus");
    write_line_a("  openjp");
    write_line_a("  nativejp");
    write_line_a("  manualtip");
    write_line_a("  manualtipsnap");
    write_line_a("  delay <ms>");
    write_line_a("  type <ascii>");
    write_line_a("  interim <on|off>");
    write_line_a("  status <normal|transitory>");
    write_line_a("  probe <convert|nonconvert|kana>");
    write_line_a("  strike <convert|nonconvert|kana>");
    write_line_a("  providers");
    write_line_a("  langbar");
    write_line_a("  compartment");
    write_line_a("  categorymgr");
    write_line_a("  qi");
    write_line_a("  tmex");
    write_line_a("  tmexsnap");
    write_line_a("  snapshot");
    write_line_a("  selection");
    write_line_a("  ime");
    write_line_a("  drive");
    write_line_a("  sleep <ms>");
    write_line_a("  dump");
    write_line_a("  -- candidate/conversion (warm-loop, no rebuild) --");
    write_line_a("  reset                 clear composition for a fresh test");
    write_line_a("  reading               commit the hiragana reading (RETURN)");
    write_line_a("  reconv [n]            reconvert reading; optional select index n");
    write_line_a("  funcs                 enumerate ATOK function-provider surface");
    write_line_a("  kata[kana] [f6..f10|vk]  function-key convert (default F7 katakana)");
    write_line_a("  e.g.: reset; type kisha; reading; reconv");
    write_line_a("  quit");
}

static void runtime_list_function_providers(void)
{
    IEnumTfFunctionProviders *en = 0;
    ITfFunctionProvider *prov_direct = 0;
    ITfFunctionProvider *prov = 0;
    ULONG fetched = 0;
    HRESULT hr;
    GUID type;
    BSTR desc = 0;
    char desc_a[256];
    char buf[256];

    if (!g_tm) {
        write_line_a("runtime providers: no thread manager");
        return;
    }
    hr = g_tm->lpVtbl->GetFunctionProvider(g_tm, &CLSID_ATOK_TIP, &prov_direct);
    log_hr("runtime providers GetFunctionProvider", hr);
    if (hr == S_OK && prov_direct) {
        if (prov_direct->lpVtbl && prov_direct->lpVtbl->GetType && prov_direct->lpVtbl->GetType(prov_direct, &type) == S_OK) {
            wsprintfA(buf, "runtime provider direct type=%08lX-%04X-%04X\r\n",
                      (unsigned long)type.Data1, type.Data2, type.Data3);
            write_line_a(buf);
        } else {
            write_line_a("runtime provider direct type=<failed>");
        }
        if (prov_direct->lpVtbl && prov_direct->lpVtbl->GetDescription && prov_direct->lpVtbl->GetDescription(prov_direct, &desc) == S_OK && desc) {
            WideCharToMultiByte(CP_ACP, 0, desc, -1, desc_a, sizeof(desc_a), 0, 0);
            wsprintfA(buf, "runtime provider direct desc=%s\r\n", desc_a);
            write_line_a(buf);
        }
        prov_direct->lpVtbl->Release(prov_direct);
        prov_direct = 0;
    }
    hr = g_tm->lpVtbl->EnumFunctionProviders(g_tm, &en);
    log_hr("runtime providers EnumFunctionProviders", hr);
    if (hr != S_OK || !en) return;
    while (en->lpVtbl->Next(en, 1, &prov, &fetched) == S_OK && fetched == 1) {
        if (prov && prov->lpVtbl && prov->lpVtbl->GetType && prov->lpVtbl->GetType(prov, &type) == S_OK) {
            wsprintfA(buf, "runtime provider type=%08lX-%04X-%04X\r\n",
                      (unsigned long)type.Data1, type.Data2, type.Data3);
            write_line_a(buf);
        } else {
            write_line_a("runtime provider type=<failed>");
        }
        if (prov && prov->lpVtbl && prov->lpVtbl->GetDescription && prov->lpVtbl->GetDescription(prov, &desc) == S_OK && desc) {
            WideCharToMultiByte(CP_ACP, 0, desc, -1, desc_a, sizeof(desc_a), 0, 0);
            wsprintfA(buf, "runtime provider desc=%s\r\n", desc_a);
            write_line_a(buf);
        }
        if (prov) prov->lpVtbl->Release(prov);
        prov = 0;
    }
    en->lpVtbl->Release(en);
}

static void runtime_probe_langbar(void)
{
    struct {
        const char *tag;
        IUnknown *obj;
    } probes[] = {
        { "tm", (IUnknown *)g_tm },
        { "tmex", (IUnknown *)g_tm },
        { "docmgr", (IUnknown *)g_docmgr },
        { "context", (IUnknown *)g_context },
        { "profiles", (IUnknown *)g_profiles },
        { "tip", (IUnknown *)g_tip },
    };
    size_t i;

    {
        const char *tmex_source = runtime_dump_tmex_state();
        char buf[96];
        wsprintfA(buf, "runtime langbar tmex source=%s", tmex_source);
        write_line_a(buf);
    }

    for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        ITfThreadMgrEx *tmex = 0;
        ITfLangBarItemMgr *mgr = 0;
        IEnumTfLangBarItems *en = 0;
        ITfLangBarItem *item = 0;
        ULONG fetched = 0;
        DWORD count = 0;
        HRESULT hr;
        char buf[96];

        if (!probes[i].obj) {
            wsprintfA(buf, "runtime langbar %s: no object", probes[i].tag);
            write_line_a(buf);
            continue;
        }

        if (streq_icase_a(probes[i].tag, "tmex")) {
            hr = probes[i].obj->lpVtbl->QueryInterface(probes[i].obj, &IID_LOCAL_ITfThreadMgrEx, (void **)&tmex);
            wsprintfA(buf, "runtime langbar %s QI tmex hr=0x%08lX", probes[i].tag, (unsigned long)hr);
            write_line_a(buf);
            if (hr == S_OK && tmex) {
                wsprintfA(buf, "runtime langbar %s tmex ptr=%p vtbl=%p qi=%p", probes[i].tag,
                          tmex, tmex->lpVtbl, tmex->lpVtbl ? tmex->lpVtbl->QueryInterface : 0);
                write_line_a(buf);
                hr = tmex->lpVtbl->QueryInterface(tmex, &IID_LOCAL_ITfLangBarItemMgr, (void **)&mgr);
                wsprintfA(buf, "runtime langbar %s tmex->langbar QI hr=0x%08lX", probes[i].tag, (unsigned long)hr);
                write_line_a(buf);
                tmex->lpVtbl->Release(tmex);
                tmex = 0;
            }
        } else {
            hr = probes[i].obj->lpVtbl->QueryInterface(probes[i].obj, &IID_LOCAL_ITfLangBarItemMgr, (void **)&mgr);
            wsprintfA(buf, "runtime langbar %s QI hr=0x%08lX", probes[i].tag, (unsigned long)hr);
            write_line_a(buf);
        }
        if (hr != S_OK || !mgr) continue;

        hr = mgr->lpVtbl->GetItemNum(mgr, &count);
        wsprintfA(buf, "runtime langbar %s GetItemNum hr=0x%08lX", probes[i].tag, (unsigned long)hr);
        write_line_a(buf);
        if (hr == S_OK) {
            wsprintfA(buf, "runtime langbar %s item count=%lu", probes[i].tag, (unsigned long)count);
            write_line_a(buf);
        }

        hr = mgr->lpVtbl->EnumItems(mgr, &en);
        wsprintfA(buf, "runtime langbar %s EnumItems hr=0x%08lX", probes[i].tag, (unsigned long)hr);
        write_line_a(buf);
        if (hr == S_OK && en) {
            while (en->lpVtbl->Next(en, 1, &item, &fetched) == S_OK && fetched == 1) {
                wsprintfA(buf, "runtime langbar %s item enumerated", probes[i].tag);
                write_line_a(buf);
                if (item) item->lpVtbl->Release(item);
                item = 0;
            }
            en->lpVtbl->Release(en);
        }

        mgr->lpVtbl->Release(mgr);
    }
}

static void runtime_probe_qi(void)
{
    struct {
        const char *tag;
        IUnknown *obj;
    } probes[] = {
        { "tm", (IUnknown *)g_tm },
        { "docmgr", (IUnknown *)g_docmgr },
        { "context", (IUnknown *)g_context },
    };
    size_t i;
    HRESULT hr;
    ITfThreadMgrEx *tmex = 0;

    write_line_a("runtime qi: probing activation GUIDs");
    {
        const char *tmex_source = runtime_dump_tmex_state();
        char buf[96];
        wsprintfA(buf, "runtime qi tmex source=%s", tmex_source);
        write_line_a(buf);
    }

    if (g_tm) {
        char buf[160];
        hr = g_tm->lpVtbl->QueryInterface(g_tm, &IID_LOCAL_ITfThreadMgrEx, (void **)&tmex);
        wsprintfA(buf, "runtime qi tm IID_LOCAL_ITfThreadMgrEx hr=0x%08lX", (unsigned long)hr);
        write_line_a(buf);
        if (hr == S_OK && tmex) {
            ITfCompartmentMgr *cmgr = 0;
            ITfLangBarItemMgr *lmgr = 0;

            hr = tmex->lpVtbl->QueryInterface(tmex, &IID_ITfCompartmentMgr, (void **)&cmgr);
            wsprintfA(buf, "runtime qi tmex IID_ITfCompartmentMgr hr=0x%08lX", (unsigned long)hr);
            write_line_a(buf);
            if (cmgr) cmgr->lpVtbl->Release(cmgr);

            hr = tmex->lpVtbl->QueryInterface(tmex, &IID_LOCAL_ITfLangBarItemMgr, (void **)&lmgr);
            wsprintfA(buf, "runtime qi tmex IID_LOCAL_ITfLangBarItemMgr hr=0x%08lX", (unsigned long)hr);
            write_line_a(buf);
            if (lmgr) lmgr->lpVtbl->Release(lmgr);

            tmex->lpVtbl->Release(tmex);
            tmex = 0;
        }
    }

    for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        char buf[160];
        if (!probes[i].obj) {
            wsprintfA(buf, "runtime qi %s: no object", probes[i].tag);
            write_line_a(buf);
            continue;
        }

        {
            ITfCompartmentMgr *cmgr = 0;
            ITfLangBarItemMgr *lmgr = 0;

            hr = probes[i].obj->lpVtbl->QueryInterface(probes[i].obj, &IID_ITfCompartmentMgr, (void **)&cmgr);
            wsprintfA(buf, "runtime qi %s IID_ITfCompartmentMgr hr=0x%08lX", probes[i].tag, (unsigned long)hr);
            write_line_a(buf);
            if (cmgr) {
                cmgr->lpVtbl->Release(cmgr);
            }

            hr = probes[i].obj->lpVtbl->QueryInterface(probes[i].obj, &IID_LOCAL_ITfLangBarItemMgr, (void **)&lmgr);
            wsprintfA(buf, "runtime qi %s IID_LOCAL_ITfLangBarItemMgr hr=0x%08lX", probes[i].tag, (unsigned long)hr);
            write_line_a(buf);
            if (lmgr) {
                lmgr->lpVtbl->Release(lmgr);
            }
        }
    }
}

static void runtime_snapshot(void)
{
    write_line_a("runtime snapshot: begin");
    {
        const char *tmex_source = runtime_dump_tmex_state();
        char buf[96];
        wsprintfA(buf, "runtime snapshot tmex source=%s", tmex_source);
        write_line_a(buf);
    }
    runtime_list_function_providers();
    runtime_probe_langbar();
    runtime_probe_compartments();
    runtime_probe_categorymgr();
    runtime_probe_qi();
    runtime_dump("runtime snapshot");
    write_line_a("runtime snapshot: end");
}

static void log_reg_sz_value(HKEY root, const wchar_t *subkey, const wchar_t *value_name, const char *label)
{
    HKEY hk = 0;
    wchar_t buf[512];
    DWORD type = 0;
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    LONG lr;
    char value_a[768];
    char out[1024];

    lr = RegOpenKeyExW(root, subkey, 0, KEY_READ, &hk);
    if (lr != ERROR_SUCCESS) {
        wsprintfA(out, "runtime registry %s open hr=0x%08lX", label, (unsigned long)lr);
        write_line_a(out);
        return;
    }

    ZeroMemory(buf, sizeof(buf));
    lr = RegQueryValueExW(hk, value_name, 0, &type, (LPBYTE)buf, &size);
    RegCloseKey(hk);
    if (lr != ERROR_SUCCESS) {
        wsprintfA(out, "runtime registry %s query hr=0x%08lX", label, (unsigned long)lr);
        write_line_a(out);
        return;
    }
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, value_a, (int)sizeof(value_a) - 1, 0, 0);
        value_a[sizeof(value_a) - 1] = 0;
        wsprintfA(out, "runtime registry %s value=%s", label, value_a);
        write_line_a(out);
        return;
    }
    wsprintfA(out, "runtime registry %s type=0x%08lX", label, (unsigned long)type);
    write_line_a(out);
}

static void log_clsid_registration(const wchar_t *clsid, const char *label)
{
    wchar_t key[256];
    char out[256];

    wsprintfW(key, L"Software\\Classes\\CLSID\\%s\\InprocServer32", clsid);
    wsprintfA(out, "runtime registry %s InprocServer32", label);
    write_line_a(out);
    log_reg_sz_value(HKEY_LOCAL_MACHINE, key, NULL, "HKLM");
    log_reg_sz_value(HKEY_CURRENT_USER, key, NULL, "HKCU");

    wsprintfA(out, "runtime registry %s ThreadingModel", label);
    write_line_a(out);
    log_reg_sz_value(HKEY_LOCAL_MACHINE, key, L"ThreadingModel", "HKLM");
    log_reg_sz_value(HKEY_CURRENT_USER, key, L"ThreadingModel", "HKCU");
}

static void log_tsf_assembly_registration(void)
{
    wchar_t key[256];
    wsprintfW(key, L"Software\\Microsoft\\CTF\\Assemblies\\0x00000411\\%s", L"{34745C63-B2F0-4784-8B67-5E12C8701A31}");
    write_line_a("runtime registry TSF assembly");
    log_reg_sz_value(HKEY_CURRENT_USER, key, L"Default", "HKCU Default");
    log_reg_sz_value(HKEY_CURRENT_USER, key, L"Profile", "HKCU Profile");
}

static void runtime_probe_categorymgr(void)
{
    HRESULT hr;
    ITfCategoryMgr *mgr = 0;

    write_line_a("runtime categorymgr: probing CLSID_TF_CategoryMgr");
    log_clsid_registration(L"{A4B544A1-438D-4B41-9325-869523E2D6C7}", "CLSID_TF_CategoryMgr");
    log_clsid_registration(L"{1314EB53-CACA-4152-A556-A184143202AF}", "ATOK TIP");
    log_tsf_assembly_registration();
    hr = CoCreateInstance(&CLSID_LOCAL_TF_CategoryMgr, 0, CLSCTX_INPROC_SERVER, &IID_LOCAL_ITfCategoryMgr, (void **)&mgr);
    log_hr("runtime categorymgr CoCreateInstance", hr);
    if (hr == S_OK && mgr) {
        mgr->lpVtbl->Release(mgr);
    }
}

static void run_runtime_command(char *cmd)
{
    char *arg = cmd;
    char *next = 0;
    char dbg[160];
    while (*arg == ' ' || *arg == '\t') arg++;
    if (!*arg) return;

    wsprintfA(dbg, "runtime cmd: %s", arg);
    write_line_a(dbg);

    for (next = arg; *next; next++) {
        if (*next == ';' || *next == '\n' || *next == '\r') {
            *next++ = 0;
            break;
        }
    }

    if (streq_icase_a(arg, "help")) {
        runtime_help();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "focus")) {
        force_focus(g_hwndFrame);
        write_line_a("runtime: focus set");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "openjp")) {
        {
            HKL hkl = LoadKeyboardLayoutW(L"00000411", 0x00000100);
            if (hkl) {
                ActivateKeyboardLayout(hkl, 0);
                write_line_a("runtime: keyboard layout 00000411 activated");
            }
        }
        ime_open(g_hwndFrame);
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "nativejp")) {
        DWORD conv = IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE | IME_CMODE_ROMAN;
        DWORD sent = 0;
        HIMC himc = ImmGetContext(g_hwndFrame);
        if (himc) {
            ImmSetOpenStatus(himc, 1);
            if (ImmSetConversionStatus(himc, conv, sent)) {
                write_line_a("runtime: nativejp set");
            }
            ImmReleaseContext(g_hwndFrame, himc);
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "sleep")) {
        DWORD v = 0;
        char *p = arg + 5;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (DWORD)(*p - '0');
            p++;
        }
        if (v) {
            Sleep(v);
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "delay")) {
        DWORD v = get_env_dword(L"AT_TYPE_DELAY_MS", 180);
        char *p = arg + 5;
        DWORD n = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p) {
            char *q = p;
            n = 0;
            while (*q >= '0' && *q <= '9') {
                n = n * 10 + (DWORD)(*q - '0');
                q++;
            }
            if (q != p) v = n;
        }
        g_runtime_type_delay_ms = v;
        {
            char buf[96];
            wsprintfA(buf, "runtime: type delay=%lu ms", (unsigned long)g_runtime_type_delay_ms);
            write_line_a(buf);
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "interim")) {
        char *p = arg + 7;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;
        if (streq_icase_a(p, "on") || streq_icase_a(p, "1") || streq_icase_a(p, "true")) {
            g_runtime_interim_input = TRUE;
            TextStore_SetInterimSelection(TRUE);
            write_line_a("runtime: interim input ON");
        } else if (streq_icase_a(p, "off") || streq_icase_a(p, "0") || streq_icase_a(p, "false")) {
            g_runtime_interim_input = FALSE;
            TextStore_SetInterimSelection(FALSE);
            write_line_a("runtime: interim input OFF");
        } else {
            char buf[96];
            wsprintfA(buf, "runtime: interim input=%d", g_runtime_interim_input ? 1 : 0);
            write_line_a(buf);
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "status")) {
        char *p = arg + 6;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;
        if (streq_icase_a(p, "normal") || streq_icase_a(p, "0")) {
            TextStore_SetStaticFlags(0);
            write_line_a("runtime: text-store status NORMAL");
        } else if (streq_icase_a(p, "transitory") || streq_icase_a(p, "transient")) {
            TextStore_SetStaticFlags(TS_SS_TRANSITORY);
            write_line_a("runtime: text-store status TRANSITORY");
        } else {
            char buf[96];
            wsprintfA(buf, "runtime: text-store status=0x%08lX", (unsigned long)TS_SS_TRANSITORY);
            write_line_a(buf);
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "manualtip")) {
        HRESULT hr = E_FAIL;
        ITfTextInputProcessor *tip = 0;
        if (!g_tm) {
            write_line_a("runtime: no thread manager");
            return;
        }
        {
            char buf[64];
            wsprintfA(buf, "runtime: manualtip client_id=%lu", (unsigned long)g_client_id);
            write_line_a(buf);
        }
        install_ns_shim();
        preload_tip_dependencies();
        hr = CoCreateInstance(&CLSID_ATOK_TIP, 0, CLSCTX_INPROC_SERVER, &IID_ITfTextInputProcessor, (void **)&tip);
        log_hr("runtime manualtip CoCreateInstance", hr);
        if (hr == S_OK && tip) {
            patch_ns_shim_now();
            g_tip = tip;
            hr = tip->lpVtbl->Activate(tip, g_tm, g_client_id);
            log_hr("runtime manualtip Activate", hr);
            if (hr != S_OK) {
                tip->lpVtbl->Release(tip);
                g_tip = 0;
            }
        }
        runtime_dump("runtime manualtip");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "manualtipsnap")) {
        write_line_a("runtime manualtipsnap: begin");
        {
            char manual_line[] = "manualtip";
            run_runtime_command(manual_line);
        }
        runtime_snapshot();
        write_line_a("runtime manualtipsnap: end");
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "type")) {
        char *p = arg + 4;
        DWORD type_delay_ms;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;
        type_delay_ms = get_type_delay_ms();
        send_ascii_text_a(g_km, p);
        pump_messages((int)(type_delay_ms + 100));
        runtime_dump("runtime type");
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "probe")) {
        char *mode = arg + 5;
        while (*mode == ' ' || *mode == '\t') mode++;
        if (*mode == '=') mode++;
        while (*mode == ' ' || *mode == '\t') mode++;
        if (streq_icase_a(mode, "convert") || streq_icase_a(mode, "henkan")) {
            send_key(g_km, VK_CONVERT, FALSE);
        } else if (streq_icase_a(mode, "nonconvert") || streq_icase_a(mode, "muhenkan")) {
            send_key(g_km, 0x1D, FALSE);
        } else if (streq_icase_a(mode, "kana")) {
            send_key(g_km, 0x19, FALSE);
        } else {
            send_key(g_km, VK_SPACE, FALSE);
        }
        Sleep(200);
        runtime_dump("runtime probe");
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "strike")) {
        char *mode = arg + 6;
        while (*mode == ' ' || *mode == '\t') mode++;
        if (*mode == '=') mode++;
        while (*mode == ' ' || *mode == '\t') mode++;
        if (streq_icase_a(mode, "convert") || streq_icase_a(mode, "henkan")) {
            send_key(g_km, VK_CONVERT, TRUE);
        } else if (streq_icase_a(mode, "nonconvert") || streq_icase_a(mode, "muhenkan")) {
            send_key(g_km, 0x1D, TRUE);
        } else if (streq_icase_a(mode, "kana")) {
            send_key(g_km, 0x19, TRUE);
        } else {
            send_key(g_km, VK_SPACE, TRUE);
        }
        Sleep(200);
        runtime_dump("runtime strike");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "drive")) {
        if (g_km && g_context) {
            drive_conversion(g_km, g_context);
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "dump")) {
        runtime_dump("runtime dump");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "providers")) {
        runtime_list_function_providers();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "langbar")) {
        runtime_probe_langbar();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "compartment")) {
        runtime_probe_compartments();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "categorymgr")) {
        runtime_probe_categorymgr();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "tmex")) {
        {
            const char *tmex_source = runtime_dump_tmex_state();
            char buf[96];
            wsprintfA(buf, "runtime tmex source=%s", tmex_source);
            write_line_a(buf);
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "tmexsnap")) {
        write_line_a("runtime tmexsnap: begin");
        runtime_snapshot();
        write_line_a("runtime tmexsnap: end");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "qi")) {
        runtime_probe_qi();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "snapshot")) {
        runtime_snapshot();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "selection")) {
        runtime_probe_selection();
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "ime")) {
        runtime_probe_ime(g_hwndFrame, "runtime");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "reset")) {
        /* Clean slate for the next scripted test, no restart. */
        if (!pMsctfShim_Reset) resolve_shim_text_bridge();
        if (pMsctfShim_Reset) pMsctfShim_Reset();
        g_reconv_select = 0xFFFFFFFF;
        write_line_a("runtime: reset (comp cleared)");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "reading")) {
        /* Commit the current hiragana reading (RETURN, no henkan) so it can be
         * reconverted — the working candidate-list path. */
        send_key(g_km, VK_RETURN, FALSE);
        pump_messages((int)(get_type_delay_ms() + 100));
        runtime_dump("runtime reading");
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "funcs")) {
        /* Enumerate ATOK's TSF function-provider surface live. */
        if (g_tm) {
            ITfFunctionProvider *prov = 0;
            HRESULT hr = g_tm->lpVtbl->GetFunctionProvider(g_tm, &CLSID_ATOK_TIP, &prov);
            log_hr("runtime funcs GetFunctionProvider", hr);
            if (hr == S_OK && prov) {
                probe_function_provider(prov);
                prov->lpVtbl->Release(prov);
            }
        }
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "reconv")) {
        /* reconv [n] — reconvert the committed reading; optional select index n.
         * Runs forced (ignores the AT_PROBE_RECONV env gate). */
        char *p = arg + 6;
        while (*p == ' ' || *p == '\t' || *p == '=') p++;
        if (*p >= '0' && *p <= '9') {
            DWORD v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (DWORD)(*p - '0'); p++; }
            g_reconv_select = v;
        } else {
            g_reconv_select = 0xFFFFFFFF;
        }
        probe_reconversion(g_context, "ctl reconv", TRUE);
        g_reconv_select = 0xFFFFFFFF;
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "katakana") || starts_with_a(arg, "kata")) {
        /* kata[kana] [vk|f6..f10] — function-key convert of the current reading;
         * default F7 (full-width). F8 half-width, F9/F10 alnum, F6 hiragana. */
        char *p = arg + (starts_with_a(arg, "katakana") ? 8 : 4);
        WPARAM vk = VK_F7;
        while (*p == ' ' || *p == '\t' || *p == '=') p++;
        if ((p[0] == 'f' || p[0] == 'F') && p[1] >= '0' && p[1] <= '9') {
            DWORD n = 0; p++;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (DWORD)(*p - '0'); p++; }
            if (n >= 1 && n <= 10) vk = VK_F1 + (n - 1);
        } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            DWORD n = 0; p += 2;
            while (*p) {
                DWORD d;
                if (*p >= '0' && *p <= '9') d = (DWORD)(*p - '0');
                else if (*p >= 'a' && *p <= 'f') d = (DWORD)(*p - 'a' + 10);
                else if (*p >= 'A' && *p <= 'F') d = (DWORD)(*p - 'A' + 10);
                else break;
                n = n * 16 + d; p++;
            }
            if (n) vk = n;
        } else if (*p >= '0' && *p <= '9') {
            DWORD n = 0;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (DWORD)(*p - '0'); p++; }
            if (n) vk = n;
        }
        {
            char kb[80];
            wsprintfA(kb, "runtime katakana VK=0x%02lX", (unsigned long)vk);
            write_line_a(kb);
        }
        send_key(g_km, vk, FALSE);
        pump_messages((int)(get_type_delay_ms() * 2 + 100));
        runtime_dump("runtime katakana");
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "convert")) {
        /* convert <romaji> — the daemon result path: reset, type the romaji,
         * commit the hiragana reading, then reconvert. The structured ATD block
         * is emitted by the reconversion enumerator (at_daemon_emit_result)
         * BEFORE ATOK's edit-session teardown, which can fault. top-1 is [0]. */
        char *p = arg + 7;
        while (*p == ' ' || *p == '\t' || *p == '=') p++;
        daemon_ms("cmd");
        if (!pMsctfShim_Reset) resolve_shim_text_bridge();
        if (pMsctfShim_Reset) pMsctfShim_Reset();
        daemon_ms("reset");
        {
            /* The settle pump after typing/commit lets ATOK drain its queued
             * romaji->kana work. AT_PUMP_MS tunes it (default 100); the per-char
             * type delay is added so a non-zero AT_TYPE_DELAY_MS still applies. */
            int pump_ms = (int)(get_type_delay_ms() + get_env_dword(L"AT_PUMP_MS", 100));
            if (*p) {
                send_ascii_text_a(g_km, p);
                pump_messages(pump_ms);
            }
            daemon_ms("typed");
            send_key(g_km, VK_RETURN, FALSE);            /* commit the reading */
            pump_messages(pump_ms);
            daemon_ms("reading");
        }
        g_daemon_cap_active = 1;
        g_daemon_cap_count = 0;
        /* do_reconversion emits the ATD block, then (daemon mode) clean-exits
         * before ATOK's reconversion teardown — so this call does not return. */
        probe_reconversion(g_context, "convert", TRUE);
        g_daemon_cap_active = 0;
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "henkan")) {
        /* henkan <romaji> — the FAST top-1 path (what Windows uses): type the
         * reading, press VK_CONVERT (incremental henkan from the composition the
         * engine already parsed while typing), read the converted top-1 straight
         * out of the shim composition buffer. ~5x faster than the reconversion
         * `convert` path and higher quality; exposes only the top-1 (headless ATOK
         * auto-commits on the 2nd convert), so the full list still comes from
         * `convert`. Emits the same ATD block (COMMIT = top-1). */
        char *p = arg + 6;
        int pump = (int)get_env_dword(L"AT_PUMP_MS", 0);
        WCHAR top[256];
        char line[512];
        int wn = 0, n;
        while (*p == ' ' || *p == '\t' || *p == '=') p++;
        daemon_ms("cmd");
        if (!pMsctfShim_Reset) resolve_shim_text_bridge();
        if (pMsctfShim_Reset) pMsctfShim_Reset();
        if (*p) { send_ascii_text_a(g_km, p); pump_messages(pump); }
        daemon_ms("typed");
        send_key(g_km, VK_CONVERT, FALSE);   /* henkan → top-1 in the composition */
        daemon_ms("henkan_kd");
        pump_messages(pump);
        daemon_ms("henkan_settled");
        g_daemon_cap_active = 1;
        g_daemon_cap_count = 0;
        if (pMsctfShim_GetCompText) wn = pMsctfShim_GetCompText(top, 256);
        if (wn > 0) {
            n = WideCharToMultiByte(CP_UTF8, 0, top, wn, line, (int)sizeof(line) - 1, 0, 0);
            if (n > 0) {
                line[n] = 0;
                lstrcpynA(g_daemon_cap[0], line, AT_CAP_BYTES);
                g_daemon_cap_count = 1;
            }
        }
        at_daemon_emit_result();
        g_daemon_cap_active = 0;
        if (*next) run_runtime_command(next);
        return;
    }
    if (starts_with_a(arg, "search")) {
        /* search <reading> — the FAST result path: hand the reading straight to
         * ITfFnSearchCandidateProvider::GetSearchCandidates. No typing, no commit,
         * no edit session, no reconversion teardown. The reading is UTF-8 (the
         * caller does romaji->kana). Emits the same ATD block as convert. */
        char *p = arg + 6;
        WCHAR wq[256];
        int wn;
        while (*p == ' ' || *p == '\t' || *p == '=') p++;
        daemon_ms("cmd");
        wn = MultiByteToWideChar(CP_UTF8, 0, p, -1, wq,
                                 (int)(sizeof(wq) / sizeof(wq[0])) - 1);
        if (wn <= 0) wq[0] = 0;
        g_daemon_cap_active = 1;
        g_daemon_cap_count = 0;
        search_candidates_daemon(wq);
        g_daemon_cap_active = 0;
        if (*next) run_runtime_command(next);
        return;
    }
    if (streq_icase_a(arg, "quit")) {
        g_runtime_quit = TRUE;
        PostQuitMessage(0);
        return;
    }

    write_line_a("runtime: unknown command");
    if (*next) run_runtime_command(next);
}

static LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_IME_NOTIFY) {
        char buf[160];
        const char *name = ime_notify_name(wp);
        if (name) {
            wsprintfA(buf, "WM_IME_NOTIFY wp=0x%08lX (%s)", (unsigned long)wp, name);
        } else {
            wsprintfA(buf, "WM_IME_NOTIFY wp=0x%08lX", (unsigned long)wp);
        }
        write_line_a(buf);
        log_ime_status(hwnd, "IMM after WM_IME_NOTIFY");
    } else if (msg == WM_IME_SETCONTEXT) {
        char buf[128];
        wsprintfA(buf, "WM_IME_SETCONTEXT wp=0x%08lX lp=0x%08lX", (unsigned long)wp, (unsigned long)lp);
        write_line_a(buf);
        log_ime_status(hwnd, "IMM after WM_IME_SETCONTEXT");
    } else if (msg == WM_IME_REQUEST) {
        char buf[128];
        wsprintfA(buf, "WM_IME_REQUEST wp=0x%08lX lp=0x%08lX", (unsigned long)wp, (unsigned long)lp);
        write_line_a(buf);
        log_ime_status(hwnd, "IMM after WM_IME_REQUEST");
    } else if (msg == WM_IME_STARTCOMPOSITION) {
        write_line_a("WM_IME_STARTCOMPOSITION");
        log_ime_status(hwnd, "IMM after WM_IME_STARTCOMPOSITION");
        log_ime_composition(hwnd);
    } else if (msg == WM_IME_COMPOSITION) {
        char buf[192];
        wsprintfA(buf, "WM_IME_COMPOSITION wp=0x%08lX lp=0x%08lX", (unsigned long)wp, (unsigned long)lp);
        write_line_a(buf);
        log_ime_status(hwnd, "IMM after WM_IME_COMPOSITION");
        log_ime_composition(hwnd);
    } else if (msg == WM_IME_ENDCOMPOSITION) {
        write_line_a("WM_IME_ENDCOMPOSITION");
        log_ime_status(hwnd, "IMM after WM_IME_ENDCOMPOSITION");
        log_ime_composition(hwnd);
    } else if (msg == WM_CHAR) {
        char buf[96];
        wsprintfA(buf, "WM_CHAR ch=U+%04lX", (unsigned long)wp);
        write_line_a(buf);
        if (!pMsctfShim_AppendChar) {
            resolve_shim_text_bridge();
        }
        if (pMsctfShim_AppendChar) {
            pMsctfShim_AppendChar((WCHAR)wp);
        }
        if (!g_runtime_interim_input) {
            TextStore_AppendChar((WCHAR)wp);
        }
    }

    if (msg == WM_KEYDOWN && g_km && g_use_keystroke_mgr) {
        HRESULT hr_test = E_FAIL;
        HRESULT hr_key = E_FAIL;
        BOOL eaten = FALSE;
        if (g_km->lpVtbl->TestKeyDown) {
            hr_test = g_km->lpVtbl->TestKeyDown(g_km, wp, lp, &eaten);
        }
        if (hr_test == S_OK && eaten && g_km->lpVtbl->KeyDown) {
            hr_key = g_km->lpVtbl->KeyDown(g_km, wp, lp, &eaten);
            if (hr_key == S_OK && eaten) {
                return 0;
            }
        }
    }
    if (msg == WM_KEYUP && g_km && g_use_keystroke_mgr) {
        HRESULT hr_test = E_FAIL;
        HRESULT hr_key = E_FAIL;
        BOOL eaten = FALSE;
        if (g_km->lpVtbl->TestKeyUp) {
            hr_test = g_km->lpVtbl->TestKeyUp(g_km, wp, lp, &eaten);
        }
        if (hr_test == S_OK && eaten && g_km->lpVtbl->KeyUp) {
            hr_key = g_km->lpVtbl->KeyUp(g_km, wp, lp, &eaten);
            if (hr_key == S_OK && eaten) {
                return 0;
            }
        }
    }
    if (msg == WM_COPYDATA) {
        char buf[128];
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)(ULONG_PTR)lp;
        wsprintfA(buf, "WM_COPYDATA mode=%d lp=%p", g_runtime_mode ? 1 : 0, (void *)cds);
        write_line_a(buf);
        if (cds) {
            wsprintfA(buf, "WM_COPYDATA cbData=%lu data=%p", (unsigned long)cds->cbData, cds->lpData);
            write_line_a(buf);
        }
        if (g_runtime_mode && cds && cds->lpData && cds->cbData > 0) {
            run_runtime_command((char *)cds->lpData);
            return 1;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ime_open(HWND hwnd)
{
    HIMC himc = ImmGetContext(hwnd);
    if (himc) {
        ImmSetOpenStatus(himc, TRUE);
        ImmReleaseContext(hwnd, himc);
        log_str("IME open=ON\r\n");
    }
}

static HWND create_host_window(void)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = FrameWndProc;
    wc.hInstance = GetModuleHandleW(0);
    wc.lpszClassName = L"AtTipHostFrame";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_hwndFrame = CreateWindowExW(0, wc.lpszClassName, L"AtTipHost (focus here: type romaji)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 480, 120, 0, 0, wc.hInstance, 0);
    ShowWindow(g_hwndFrame, SW_SHOW);
    force_focus(g_hwndFrame);
    return g_hwndFrame;
}

static HRESULT activate_atok_profile(ITfInputProcessorProfiles *profiles)
{
    HRESULT hr;
    CLSID def_clsid;
    GUID def_guid;
    TF_LANGUAGEPROFILE prof;
    ULONG fetched;
    IEnumTfLanguageProfiles *en = 0;

    hr = profiles->lpVtbl->ChangeCurrentLanguage(profiles, LANGID_JA);
    log_hr("ChangeCurrentLanguage", hr);

    hr = profiles->lpVtbl->GetDefaultLanguageProfile(
        profiles, LANGID_JA, &GUID_TFCAT_TIP_KEYBOARD_LOCAL, &def_clsid, &def_guid);
    log_hr("GetDefaultLanguageProfile", hr);
    if (hr == S_OK && IsEqualCLSID(&def_clsid, &CLSID_ATOK_TIP)) {
        hr = profiles->lpVtbl->ActivateLanguageProfile(profiles, &CLSID_ATOK_TIP, LANGID_JA, &def_guid);
        log_hr("ActivateLanguageProfile(default)", hr);
        return hr;
    }

    hr = profiles->lpVtbl->ActivateLanguageProfile(profiles, &CLSID_ATOK_TIP, LANGID_JA, &GUID_ATOK_PROFILE);
    log_hr("ActivateLanguageProfile(fallback)", hr);

    hr = profiles->lpVtbl->EnumLanguageProfiles(profiles, LANGID_JA, &en);
    if (hr != S_OK || !en) return hr;
    while (en->lpVtbl->Next(en, 1, &prof, &fetched) == S_OK && fetched == 1) {
        if (IsEqualCLSID(&prof.clsid, &CLSID_ATOK_TIP)) {
            hr = profiles->lpVtbl->ActivateLanguageProfile(
                profiles, &CLSID_ATOK_TIP, LANGID_JA, &prof.guidProfile);
            log_hr("ActivateLanguageProfile(enum)", hr);
            en->lpVtbl->Release(en);
            return hr;
        }
    }
    en->lpVtbl->Release(en);
    log_str("ATOK profile not found in EnumLanguageProfiles\r\n");
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

static void send_key(ITfKeystrokeMgr *km, WPARAM vk, BOOL allow_fallback)
{
    INPUT in[2];
    UINT scan = 0;
    LPARAM lparam_down = 1 | ((LPARAM)scan << 16);
    LPARAM lparam_up = lparam_down | ((LPARAM)1 << 30) | ((LPARAM)1 << 31);
    UINT sent;
    if (vk >= 'A' && vk <= 'Z') {
        static const BYTE alpha_scans[26] = {
            0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c
        };
        scan = alpha_scans[vk - 'A'];
    } else if (vk == VK_CONVERT) {
        scan = 0x79;
    } else if (vk == VK_SPACE) {
        scan = 0x39;
    } else if (vk == VK_RETURN) {
        scan = 0x1c;
    } else if (vk >= VK_F1 && vk <= VK_F10) {
        /* set-1 make codes F1..F10; ATOK reads F6-F10 as its
         * hiragana/katakana/alnum conversion keys. */
        static const BYTE fkey_scans[10] = {
            0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44
        };
        scan = fkey_scans[vk - VK_F1];
    }
    if (!allow_fallback &&
        env_flag_enabled(L"AT_FALLBACK_ACTION_KEYS") &&
        (vk == VK_CONVERT || vk == VK_SPACE || vk == VK_RETURN)) {
        allow_fallback = TRUE;
    }
    lparam_down = 1 | ((LPARAM)scan << 16);
    lparam_up = lparam_down | ((LPARAM)1 << 30) | ((LPARAM)1 << 31);
    if (g_use_keystroke_mgr && km) {
        HRESULT hr_test_down = E_FAIL;
        HRESULT hr_down = E_FAIL;
        HRESULT hr_test_up = E_FAIL;
        HRESULT hr_up = E_FAIL;
        BOOL eaten_test_down = FALSE;
        BOOL eaten_down = FALSE;
        BOOL eaten_test_up = FALSE;
        BOOL eaten_up = FALSE;
        char buf[160];

        /* Fast path (AT_FAST_KEYS): the TSF spec lets a TIP eat a key on KeyDown
         * directly; TestKeyDown/TestKeyUp are advisory probes. Skipping them halves
         * the per-key IPC round-trips into ATOK, and skipping the per-key log line
         * avoids a tipruntime.log write per keystroke. */
        if (!g_fast_keys)
            hr_test_down = km->lpVtbl->TestKeyDown(km, vk, lparam_down, &eaten_test_down);
        hr_down = km->lpVtbl->KeyDown(km, vk, lparam_down, &eaten_down);
        if (!g_fast_keys)
            hr_test_up = km->lpVtbl->TestKeyUp(km, vk, lparam_up, &eaten_test_up);
        hr_up = km->lpVtbl->KeyUp(km, vk, lparam_up, &eaten_up);
        if (!g_fast_keys) {
            wsprintfA(buf,
                "  KeystrokeMgr vk=0x%02lX testDown=0x%08lX/%d down=0x%08lX/%d testUp=0x%08lX/%d up=0x%08lX/%d\r\n",
                (unsigned long)vk,
                (unsigned long)hr_test_down, eaten_test_down,
                (unsigned long)hr_down, eaten_down,
                (unsigned long)hr_test_up, eaten_test_up,
                (unsigned long)hr_up, eaten_up);
            log_str(buf);
        }
        (void)hr_test_down; (void)hr_test_up; (void)hr_up;
        (void)eaten_test_down; (void)eaten_test_up; (void)eaten_up;
        if (hr_down == S_OK && eaten_down) {
            if (!(env_flag_enabled(L"AT_SENDINPUT_EATEN_ACTION_KEYS") &&
                  (vk == VK_CONVERT || vk == VK_SPACE || vk == VK_RETURN))) {
                return;
            }
            log_str("  KeystrokeMgr ate action key; also sending via SendInput\r\n");
            allow_fallback = TRUE;
        }
        if (!allow_fallback) {
            log_str("  KeystrokeMgr did not eat key; no fallback\r\n");
            return;
        }
        log_str("  KeystrokeMgr did not eat key; falling back to SendInput\r\n");
    }

    ZeroMemory(in, sizeof(in));
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = (WORD)vk;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    sent = SendInput(2, in, sizeof(INPUT));
    if (sent != 2) {
        log_hr("SendInput", GetLastError());
    } else {
        log_str("  key sent via SendInput\r\n");
    }
}

static void log_ime_line(const char *tag, const WCHAR *ws)
{
    char line[512];
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, line, sizeof(line) - 1, 0, 0);
    log_str(tag);
    if (n > 0) log_str(line);
    log_str("\r\n");
}

static void log_ime_blob_len(const char *tag, HIMC himc, DWORD idx)
{
    LONG len;
    char buf[128];

    len = ImmGetCompositionStringW(himc, idx, 0, 0);
    wsprintfA(buf, "IME %s len=%ld\r\n", tag, (long)len);
    log_str(buf);
}

static void log_ime_composition(HWND hwnd)
{
    HIMC himc;
    LONG len;
    WCHAR buf[512];
    himc = ImmGetContext(hwnd);
    if (!himc) return;
    len = ImmGetCompositionStringW(himc, GCS_COMPSTR, 0, 0);
    if (len > 0 && len < (LONG)sizeof(buf) - 2) {
        ImmGetCompositionStringW(himc, GCS_COMPSTR, buf, len);
        buf[len / 2] = 0;
        log_ime_line("IME COMPSTR: ", buf);
    }
    len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, 0, 0);
    if (len > 0 && len < (LONG)sizeof(buf) - 2) {
        ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf, len);
        buf[len / 2] = 0;
        log_ime_line("IME RESULT: ", buf);
    }
    ImmReleaseContext(hwnd, himc);
}

static void runtime_probe_ime(HWND hwnd, const char *tag)
{
    HIMC himc;
    DWORD conv = 0;
    DWORD sent = 0;
    char buf[128];

    if (!hwnd) {
        write_line_a("runtime ime: no window");
        return;
    }

    himc = ImmGetContext(hwnd);
    if (!himc) {
        write_line_a("runtime ime: no context");
        return;
    }

    wsprintfA(buf, "runtime ime [%s] open=%d\r\n", tag ? tag : "", ImmGetOpenStatus(himc) ? 1 : 0);
    log_str(buf);
    if (ImmGetConversionStatus(himc, &conv, &sent)) {
        wsprintfA(buf, "runtime ime [%s] conv=0x%08lX sent=0x%08lX\r\n", tag ? tag : "",
                  (unsigned long)conv, (unsigned long)sent);
    } else {
        wsprintfA(buf, "runtime ime [%s] conv=<failed>\r\n", tag ? tag : "");
    }
    log_str(buf);

    log_ime_blob_len("GCS_COMPSTR", himc, GCS_COMPSTR);
    log_ime_blob_len("GCS_RESULTSTR", himc, GCS_RESULTSTR);
    log_ime_blob_len("GCS_COMPREADSTR", himc, GCS_COMPREADSTR);
    log_ime_blob_len("GCS_RESULTREADSTR", himc, GCS_RESULTREADSTR);
    log_ime_blob_len("GCS_COMPATTR", himc, GCS_COMPATTR);
    log_ime_blob_len("GCS_COMPCLAUSE", himc, GCS_COMPCLAUSE);
    log_ime_blob_len("GCS_RESULTCLAUSE", himc, GCS_RESULTCLAUSE);
    log_ime_blob_len("GCS_CURSORPOS", himc, GCS_CURSORPOS);
    ImmReleaseContext(hwnd, himc);
}

static void drive_conversion(ITfKeystrokeMgr *km, ITfContext *context)
{
    static const WPARAM romaji_default[] = { 'N', 'I', 'H', 'O', 'N', 'G', 'O' };
    WPARAM romaji[64];
    unsigned romaji_len = 0;
    DWORD type_delay_ms = get_type_delay_ms();
    unsigned i;
    WCHAR env_romaji[64];

    /* AT_ROMAJI overrides the romaji input (uppercased ASCII letters), so we
     * can probe whether different words produce multi-candidate behavior. */
    if (GetEnvironmentVariableW(L"AT_ROMAJI", env_romaji,
                                (DWORD)(sizeof(env_romaji) / sizeof(env_romaji[0]))) > 0) {
        for (i = 0; env_romaji[i] && romaji_len < 63; i++) {
            WCHAR c = env_romaji[i];
            if (c >= L'a' && c <= L'z') c = (WCHAR)(c - (L'a' - L'A'));
            romaji[romaji_len++] = (WPARAM)c;
        }
    }
    if (romaji_len == 0) {
        for (i = 0; i < sizeof(romaji_default) / sizeof(romaji_default[0]); i++)
            romaji[romaji_len++] = romaji_default[i];
    }

    log_str("\r\n=== drive conversion (romaji -> henkan -> space) ===\r\n");
    log_ce_snapshot("before keys");
    TextStore_LogContent();

    for (i = 0; i < romaji_len; i++) {
        char buf[32];
        wsprintfA(buf, "KeyDown '%c'\r\n", (char)romaji[i]);
        log_str(buf);
        send_key(km, romaji[i], TRUE);
        pump_messages((int)type_delay_ms);
    }
    TextStore_LogContent();
    pump_messages((int)type_delay_ms * 2);
    /* Does typing alone pre-compute candidates anywhere readable? If the engine
     * caches conversions incrementally during input, the candidate surfaces would
     * be in shared memory / IMM BEFORE any henkan key — a zero-trigger fast path. */
    scan_ce_strings("after-typing");
    dump_imm_candidates(g_hwndFrame, "after-typing");
    probe_reconversion(context, "reading (pre-henkan)", FALSE);

    if (env_flag_enabled(L"AT_KATAKANA")) {
        /* ATOK function-key conversions of the current reading: F6 hiragana,
         * F7 full-width katakana, F8 half-width katakana, F9 full-width alnum,
         * F10 half-width alnum. F7 is the katakana the modore KATAKANA flag
         * wants. AT_KATAKANA_VK overrides (decimal/0x VK, default 0x76 = F7).
         * Then commit with RETURN and read the store. */
        WPARAM kvk = (WPARAM)get_env_dword(L"AT_KATAKANA_VK", VK_F7);
        char kb[80];
        wsprintfA(kb, "KeyDown VK=0x%02lX (katakana/function-key convert)\r\n",
                  (unsigned long)kvk);
        log_str(kb);
        send_key(km, kvk, FALSE);
        pump_messages((int)type_delay_ms * 3);
        TextStore_LogContent();
        log_ime_composition(g_hwndFrame);
        Composition_PollContext(context, "after katakana key");
        log_ce_snapshot("after katakana key");
        log_str("KeyDown VK_RETURN (commit katakana)\r\n");
        send_key(km, VK_RETURN, FALSE);
        pump_messages((int)type_delay_ms * 3);
        TextStore_LogContent();
        return;
    }

    if (env_flag_enabled(L"AT_COMMIT_READING")) {
        /* Commit the hiragana reading directly (no henkan), then reconvert it.
         * Reconverting a kana reading returns ATOK's FULL conversion candidate
         * list, vs. reconverting an already-converted kanji which gives ~1.
         * This is the completeness test + the modore-shaped path (reading->cands). */
        log_str("KeyDown VK_RETURN (commit reading, no henkan)\r\n");
        send_key(km, VK_RETURN, FALSE);
        pump_messages((int)type_delay_ms * 3);
        TextStore_LogContent();
        log_ce_snapshot("after commit-reading");
        /* Try the direct query->candidate-list surface first (no edit session
         * needed); may ExitProcess if AT_SEARCH_EXIT is set. */
        probe_search_candidates();
        probe_reconversion(context, "committed reading", FALSE);
        return;
    }

    log_str("KeyDown VK_CONVERT (henkan)\r\n");
    send_key(km, VK_CONVERT, FALSE);
    pump_messages((int)type_delay_ms * 3);
    TextStore_LogContent();
    log_ime_composition(g_hwndFrame);
    Composition_PollContext(context, "after henkan");
    log_ce_snapshot("after henkan");
    scan_ce_strings("after henkan");
    dump_imm_candidates(g_hwndFrame, "after henkan");
    probe_reconversion(context, "after henkan", FALSE);

    if (env_flag_enabled(L"AT_DRIVE_CANDIDATES")) {
        /* Open + page ATOK's candidate-selection window. The first henkan picks
         * the top candidate; repeated henkan/space opens the alternatives list.
         * Each press should fire UIElementMgr Begin/UpdateUIElement in the shim,
         * where the candidate-list introspection runs. Gated so the default
         * top-1 commit run is unchanged. */
        /* ATOK's canonical "open/next candidate" key is repeated 変換
         * (VK_CONVERT), not space; space after the first convert often just
         * confirms. AT_CAND_KEY_SPACE=1 forces space for comparison. */
        WPARAM cand_vk = env_flag_enabled(L"AT_CAND_KEY_SPACE") ? VK_SPACE : VK_CONVERT;
        const char *cand_kn = (cand_vk == VK_SPACE) ? "VK_SPACE" : "VK_CONVERT";
        unsigned c;
        for (c = 0; c < 4; c++) {
            char cb[64];
            wsprintfA(cb, "KeyDown %s (cand cycle %u)\r\n", cand_kn, c + 1);
            log_str(cb);
            send_key(km, cand_vk, FALSE);
            pump_messages((int)type_delay_ms * 3);
            log_ime_composition(g_hwndFrame);
            Composition_PollContext(context, "after cand cycle");
            log_ce_snapshot("after cand cycle");
            scan_ce_strings("after cand cycle");
            dump_imm_candidates(g_hwndFrame, "after cand cycle");
        }
        /* If cycling made ATOK build its candidate-info (使い分け) pane, any
         * comment text is now resident — scan the heap for annotation markers. */
        if (env_flag_enabled(L"AT_SCAN_META"))
            scan_heap_comment_markers("after cand cycle");
    }

    log_str("KeyDown VK_SPACE (commit candidate)\r\n");
    send_key(km, VK_SPACE, FALSE);
    pump_messages((int)type_delay_ms * 3);
    TextStore_LogContent();
    log_ime_composition(g_hwndFrame);
    Composition_PollContext(context, "after space");
    log_ce_snapshot("after space");
    /* Reconversion needs NO active composition (it is for committed text). After
     * the space-commit ATOK has ended its composition, so this is the moment to
     * ask ATOK for the candidate list of the just-committed text. */
    probe_reconversion(context, "after commit", FALSE);

    log_str("KeyDown VK_RETURN\r\n");
    send_key(km, VK_RETURN, FALSE);
    pump_messages((int)type_delay_ms * 3);
    TextStore_LogContent();
    log_ce_snapshot("after return");

    {
        char buf[64];
        wsprintfA(buf, "composition callbacks: %lu\r\n", (unsigned long)TextStore_GetCompositionCount());
        log_str(buf);
    }
}

int main(void)
{
    HRESULT hr;
    HRESULT profile_hr = E_FAIL;
    wchar_t machine[64], ver[32];
    ITfThreadMgr *tm = 0;
    ITfDocumentMgr *docmgr = 0, *docmgr_prev = 0;
    ITfContext *context = 0;
    ITfInputProcessorProfiles *profiles = 0;
    ITfTextInputProcessor *tip = 0;
    ITfKeystrokeMgr *km = 0;
    IUnknown *store = 0;
    TfEditCookie edit_cookie = 0;
    TfClientId client_id = 0;
    WCHAR try_tip_buf[8];
    WCHAR skip_profile_buf[8];
    WCHAR use_km_buf[8];
    WCHAR runtime_buf[8];
    DWORD hold_ms;
    BOOL skip_profile_activation = FALSE;
    BOOL profile_after_tsf = TRUE;

    g_tick0 = GetTickCount();
    g_timing = env_flag_enabled(L"AT_TIPLOAD_TIMING");
    g_fast_keys = env_flag_enabled(L"AT_FAST_KEYS");
    g_quiet = env_flag_enabled(L"AT_QUIET");
    log_str("AtTipLoad: conversion host\r\n");
    log_pid("AtTipLoad");
    load_registry_names(machine, 64, ver, 32);
    if (env_flag_enabled(L"AT_TIPLOAD_RUNTIME")) {
        g_runtime_mode = TRUE;
    }
    if (env_flag_enabled(L"AT_TIPLOAD_DAEMON")) {
        g_runtime_mode = TRUE;   /* daemon mode is a runtime mode with a wire protocol on stdio */
        g_daemon_mode = TRUE;
    }
    if (env_flag_enabled(L"AT_TIPLOAD_SNAPSHOT_ONLY")) {
        g_runtime_snapshot_only = TRUE;
    }
    if (env_flag_enabled(L"AT_SKIP_PROFILE_ACTIVATION")) {
        skip_profile_activation = TRUE;
    }
    if (env_flag_enabled(L"AT_PROFILE_BEFORE_TSF")) {
        profile_after_tsf = FALSE;
    }
    hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    log_hr("CoInitializeEx", hr);
    preload_atok_engine_dependency();
    if (!load_msctf()) {
        log_str("msctf.dll missing exports\r\n");
        return 1;
    }

    daemon_ms("boot");
    create_host_window();
    TextStore_SetHostWindow(g_hwndFrame);
    pump_messages(200);
    daemon_ms("win");

    {
        WCHAR skip_ns[8];
        /* AtNsShim must load before TF_CreateThreadMgr: a late LoadLibrary of the
         * shim DLL page-faults in DllMain once msctf wrappers are active. */
        if (GetEnvironmentVariableW(L"AT_DEFER_NSSHIM", skip_ns,
                                    (DWORD)(sizeof(skip_ns) / sizeof(skip_ns[0]))) > 0 &&
            skip_ns[0] != L'0') {
            log_str("AtTipLoad: deferring AtNsShim until after TIP load\r\n");
        } else {
            install_ns_shim();
        }
    }
    resolve_shim_text_bridge();

    if (!profile_after_tsf) {
        hr = pTF_CreateInputProcessorProfiles(&profiles);
        log_hr("TF_CreateInputProcessorProfiles(pre-tsf)", hr);
        g_profiles = profiles;
        {
            char buf[160];
            wsprintfA(buf, "runtime profiles ptr=%p", g_profiles);
            write_line_a(buf);
        }
        if (profiles && !skip_profile_activation) {
            DWORD profile_pump_ms = get_env_dword(L"AT_PROFILE_PUMP_MS", 0);
            profile_hr = activate_atok_profile(profiles);
            if (profile_pump_ms > 0)
                pump_messages((int)profile_pump_ms);
        } else if (skip_profile_activation) {
            log_str("Profile activation skipped by AT_SKIP_PROFILE_ACTIVATION\r\n");
            profile_hr = E_FAIL;
        }
    }

    hr = pTF_CreateThreadMgr(&tm);
    log_hr("TF_CreateThreadMgr", hr);
    if (!tm) return 2;
    g_tm = tm;
    {
        char buf[192];
        wsprintfA(buf, "runtime tm ptr=%p vtbl=%p qi=%p", g_tm,
                  g_tm ? g_tm->lpVtbl : 0,
                  (g_tm && g_tm->lpVtbl) ? g_tm->lpVtbl->QueryInterface : 0);
        write_line_a(buf);
    }

    if (env_flag_enabled(L"AT_UILESS_CANDIDATES")) {
        /* UI-less mode: ActivateEx(TF_TMAE_UIELEMENTENABLEDONLY) tells compliant
         * TIPs the app renders all UI itself, so candidates should arrive as
         * ITfCandidateListUIElement (the shim introspects them) instead of the
         * TIP drawing its own window. Decisive test of whether ATOK 31 supports
         * TSF UI-less candidates. Pair with AT_UILESS_CANDIDATES in the shim. */
        ITfThreadMgrEx *tmex = 0;
        hr = tm->lpVtbl->QueryInterface(tm, &IID_LOCAL_ITfThreadMgrEx, (void **)&tmex);
        log_hr("ThreadMgr QI ITfThreadMgrEx (uiless)", hr);
        if (hr == S_OK && tmex) {
            hr = tmex->lpVtbl->ActivateEx(tmex, &client_id, TF_TMAE_UIELEMENTENABLEDONLY);
            log_hr("ThreadMgrEx::ActivateEx(UIELEMENTENABLEDONLY)", hr);
            tmex->lpVtbl->Release(tmex);
        } else {
            hr = tm->lpVtbl->Activate(tm, &client_id);
            log_hr("ThreadMgr::Activate (uiless QI failed; fallback)", hr);
        }
    } else {
        hr = tm->lpVtbl->Activate(tm, &client_id);
        log_hr("ThreadMgr::Activate", hr);
    }
    daemon_ms("tsf_act");
    g_client_id = client_id;

    store = TextStore_Create();
    if (!store) {
        log_str("TextStore_Create failed\r\n");
        return 3;
    }

    hr = tm->lpVtbl->CreateDocumentMgr(tm, &docmgr);
    log_hr("CreateDocumentMgr", hr);
    if (docmgr) {
        hr = docmgr->lpVtbl->CreateContext(docmgr, client_id, 0, store, &context, &edit_cookie);
        log_hr("CreateContext(ITextStoreACP+ITfContextOwner)", hr);
        TextStore_SetEditCookie(edit_cookie);
        g_docmgr = docmgr;
        g_context = context;
        if (context) {
            hr = docmgr->lpVtbl->Push(docmgr, context);
            log_hr("Push", hr);
            hr = CompositionSink_Advise(context);
            log_hr("CompositionSink_Advise", hr);
        }
    }

    write_line_a("main: before force_focus");
    if (env_flag_enabled(L"AT_SKIP_FORCE_FOCUS")) {
        write_line_a("main: force_focus skipped");
    } else {
        force_focus(g_hwndFrame);
        write_line_a("main: after force_focus");
    }
    if (env_flag_enabled(L"AT_SKIP_ASSOCIATE_FOCUS")) {
        write_line_a("AssociateFocus(frame) skipped by AT_SKIP_ASSOCIATE_FOCUS");
        hr = S_FALSE;
    } else if (docmgr) {
        hr = tm->lpVtbl->AssociateFocus(tm, g_hwndFrame, docmgr, &docmgr_prev);
    }
    log_hr("AssociateFocus(frame)", hr);
    if (env_flag_enabled(L"AT_SKIP_SET_FOCUS")) {
        write_line_a("SetFocus(docmgr) skipped by AT_SKIP_SET_FOCUS");
        hr = S_FALSE;
    } else if (docmgr) {
        hr = tm->lpVtbl->SetFocus(tm, docmgr);
    }
    log_hr("SetFocus(docmgr)", hr);
    set_keyboard_compartments(tm, context, client_id);

    if (profile_after_tsf || !profiles) {
        hr = pTF_CreateInputProcessorProfiles(&profiles);
        log_hr(profile_after_tsf ? "TF_CreateInputProcessorProfiles(post-tsf)" : "TF_CreateInputProcessorProfiles(retry)", hr);
        g_profiles = profiles;
        {
            char buf[160];
            wsprintfA(buf, "runtime profiles ptr=%p", g_profiles);
            write_line_a(buf);
        }
        if (profiles && !skip_profile_activation) {
            DWORD profile_pump_ms = get_env_dword(L"AT_PROFILE_PUMP_MS", 0);
            profile_hr = activate_atok_profile(profiles);
            if (profile_pump_ms > 0)
                pump_messages((int)profile_pump_ms);
        } else if (skip_profile_activation) {
            log_str("Profile activation skipped by AT_SKIP_PROFILE_ACTIVATION\r\n");
            profile_hr = E_FAIL;
        }
    }

    hr = tm->lpVtbl->QueryInterface(tm, &IID_ITfKeystrokeMgr, (void **)&km);
    log_hr("QueryInterface KeystrokeMgr", hr);
    g_km = km;
    g_use_keystroke_mgr = TRUE;
    if (GetEnvironmentVariableW(
            L"AT_USE_KEYSTROKE_MGR", use_km_buf, (DWORD)(sizeof(use_km_buf) / sizeof(use_km_buf[0]))) > 0 &&
        use_km_buf[0] == L'0') {
        g_use_keystroke_mgr = FALSE;
    }
    if (!g_use_keystroke_mgr) {
        log_str("KeystrokeMgr forwarding disabled; using SendInput only\r\n");
    } else {
        log_str("KeystrokeMgr forwarding enabled; driving ATOK through TSF sink\r\n");
    }

    pump_messages(300);
    ime_open(g_hwndFrame);
    log_ce_snapshot("idle");
    daemon_ms("ctx");

    {
        BOOL try_manual_tip = FALSE;
        BOOL skip_explicit_tip = FALSE;
        WCHAR skip_explicit_buf[8];

        if (env_flag_enabled(L"AT_TRY_MANUAL_TIP")) {
            try_manual_tip = TRUE;
        }
        if (env_flag_enabled(L"AT_SKIP_EXPLICIT_TIP")) {
            skip_explicit_tip = TRUE;
        }

        /* Defer TIP CoCreate until after the host window + keystroke mgr are ready.
         * Loading ATOK31TIP earlier page-faults under Wine once msctf wrappers are live. */
        if (!skip_explicit_tip &&
            (try_manual_tip || (!skip_profile_activation && profile_hr != S_OK))) {
            if (!skip_profile_activation && profile_hr != S_OK) {
                log_str("Profile activation did not complete; trying explicit TIP CoCreateInstance\r\n");
            } else if (try_manual_tip) {
                log_str("Manual TIP activation requested by AT_TRY_MANUAL_TIP\r\n");
            } else {
                log_str("Explicit TIP CoCreateInstance; Wine profile path does not load inproc server\r\n");
            }
            {
                DWORD load_delay_ms = get_env_dword(L"AT_TIP_LOAD_DELAY_MS", 0);
                if (load_delay_ms > 0)
                    pump_messages((int)load_delay_ms);
            }
            preload_tip_dependencies();
            daemon_ms("tip_pre");
            hr = CoCreateInstance(&CLSID_ATOK_TIP, 0, CLSCTX_INPROC_SERVER,
                                  &IID_ITfTextInputProcessor, (void **)&tip);
            log_hr("load_atok_tip: CoCreateInstance", hr);
            if (hr != S_OK || !tip) {
                hr = load_atok_tip_instance(&tip);
            }
            g_tip = tip;
            daemon_ms("tip_co");
            {
                char buf[160];
                wsprintfA(buf, "runtime tip ptr=%p", g_tip);
                write_line_a(buf);
            }
            if (hr == S_OK && tip) {
                patch_ns_shim_now();
                hr = tip->lpVtbl->Activate(tip, tm, client_id);
                log_hr("TIP::Activate", hr);
            }
            daemon_ms("tip_act");
        } else if (skip_profile_activation) {
            log_str("Manual TIP::Activate deferred by AT_SKIP_PROFILE_ACTIVATION\r\n");
        } else {
            log_str(skip_explicit_tip ?
                    "Explicit TIP::Activate skipped by AT_SKIP_EXPLICIT_TIP\r\n" :
                    "Explicit TIP::Activate skipped; profile activation owns TIP\r\n");
        }
    }
    if (g_tip == 0) {
        write_line_a("runtime tip ptr=0");
    }

    if (g_runtime_mode) {
        write_line_a("AtTipLoad runtime mode ready");
        {
            char buf[192];
            wsprintfA(buf, "runtime startup state profiles=%p tip=%p", g_profiles, g_tip);
            write_line_a(buf);
        }
        if (g_runtime_snapshot_only) {
            write_line_a("AtTipLoad: snapshot-only mode requested");
            if (!g_tip && env_flag_enabled(L"AT_SNAPSHOT_MANUALTIP")) {
                char manual_snap[] = "manualtipsnap";
                run_runtime_command(manual_snap);
            } else {
                runtime_snapshot();
            }
            goto runtime_cleanup;
        }
        runtime_snapshot();
        runtime_help();
        {
            HANDLE stdin_thread = CreateThread(0, 0, runtime_stdin_thread, 0, 0, 0);
            if (stdin_thread) {
                CloseHandle(stdin_thread);
            } else {
                write_line_a("AtTipLoad: CreateThread(stdin) failed");
            }
        }
        pump_messages_until_quit();
    } else {
        if (km)
            drive_conversion(km, context);
        hold_ms = get_env_dword(L"AT_TIPLOAD_HOLD_MS", 8000);
        {
            char buf[128];
            wsprintfA(buf, "\r\nHold %lu ms — click host window, type romaji, Space/F7 to convert, then exit.\r\n",
                      (unsigned long)hold_ms);
            log_str(buf);
        }
        pump_messages((int)hold_ms);
    }
runtime_cleanup:
    if (km) km->lpVtbl->Release(km);
    if (tip) {
        tip->lpVtbl->Deactivate(tip);
        tip->lpVtbl->Release(tip);
    }
    if (context) CompositionSink_Unadvise(context);
    if (context) context->lpVtbl->Release(context);
    if (docmgr) docmgr->lpVtbl->Release(docmgr);
    if (store) store->lpVtbl->Release(store);
    if (tm) {
        tm->lpVtbl->Deactivate(tm);
        tm->lpVtbl->Release(tm);
    }
    if (profiles) profiles->lpVtbl->Release(profiles);
    CoUninitialize();
    log_str("AtTipLoad: done\r\n");
    return 0;
}
