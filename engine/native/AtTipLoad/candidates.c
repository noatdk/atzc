/*
 * ATOK candidate / reconversion / function-provider probes.
 *
 * Extracted from AtTipLoad.c. These pull ATOK's conversion surfaces through
 * TSF without its candidate window: the reconversion candidate list
 * (ITfFnReconversion -> ITfCandidateList), the function-provider inventory, and
 * the search-candidate (prediction) provider. Shared globals/helpers come from
 * AtTipLoad.c via at_runtime.h; the local TSF interface views below are
 * private to this module.
 */
#include <windows.h>
#include <objbase.h>
#include <msctf.h>
#include "at_runtime.h"

/* TSF reconversion function + candidate list — the programmatic way to pull a
 * candidate list for a text range without ATOK's candidate window. */
static const GUID IID_LOCAL_ITfFnReconversion = {
    0x4cea93c0, 0x0a58, 0x11d3, {0x8d, 0xf0, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5}
};
static const GUID IID_LOCAL_ITfCandidateList = {
    0xa3ad50fb, 0x9bdb, 0x49e3, {0xa8, 0x43, 0x6c, 0x76, 0x52, 0x0f, 0xbf, 0x5d}
};
static const GUID GUID_LOCAL_NULL = {
    0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};
static const GUID IID_LOCAL_ITfEditSession = {
    0xaa80e803, 0x2021, 0x11d2, {0x93, 0xe0, 0x00, 0x60, 0xb0, 0x67, 0xb8, 0x6e}
};

/* Other TSF function-provider functions, probed to map ATOK's exposed surface
 * (every TSF function derives from ITfFunction, so GetDisplayName is always at
 * vtable slot 3). ITfFnSearchCandidateProvider is the prize: a query->candidate
 * call that would let the bridge skip the commit+reconvert dance entirely. */
static const GUID IID_LOCAL_ITfFnSearchCandidateProvider = {
    0x87a2ad8f, 0xf27b, 0x4920, {0x85, 0x01, 0x67, 0x60, 0x22, 0x80, 0x17, 0x5d}
};
static const GUID IID_LOCAL_ITfFnConfigure = {
    0x88f567c6, 0x1757, 0x49f8, {0xa1, 0xb2, 0x89, 0x23, 0x4c, 0x1e, 0xef, 0xf9}
};
static const GUID IID_LOCAL_ITfFnConfigureRegisterWord = {
    0xbb95808a, 0x6d8f, 0x4bca, {0x84, 0x00, 0x53, 0x90, 0xb5, 0x86, 0xae, 0xdf}
};
static const GUID IID_LOCAL_ITfFnLangProfileUtil = {
    0xa87a8574, 0xa6c1, 0x4e15, {0x99, 0xf0, 0x3d, 0x39, 0x65, 0xf5, 0x48, 0xeb}
};

/* Minimal ITfFunction view: just enough to read any TSF function's display
 * name regardless of its concrete derived interface. */
typedef struct ITfFunctionLocal ITfFunctionLocal;
typedef struct ITfFunctionLocalVtbl {
    HRESULT (WINAPI *QueryInterface)(ITfFunctionLocal *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ITfFunctionLocal *);
    ULONG (WINAPI *Release)(ITfFunctionLocal *);
    HRESULT (WINAPI *GetDisplayName)(ITfFunctionLocal *, BSTR *);
} ITfFunctionLocalVtbl;
struct ITfFunctionLocal { const ITfFunctionLocalVtbl *lpVtbl; };

typedef struct ITfFnReconversionLocal ITfFnReconversionLocal;
typedef struct ITfCandidateListLocal ITfCandidateListLocal;
typedef struct ITfCandidateStringLocal ITfCandidateStringLocal;

typedef struct ITfFnReconversionLocalVtbl {
    HRESULT (WINAPI *QueryInterface)(ITfFnReconversionLocal *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ITfFnReconversionLocal *);
    ULONG (WINAPI *Release)(ITfFnReconversionLocal *);
    HRESULT (WINAPI *GetDisplayName)(ITfFnReconversionLocal *, BSTR *);   /* ITfFunction */
    HRESULT (WINAPI *QueryRange)(ITfFnReconversionLocal *, ITfRange *, ITfRange **, BOOL *);
    HRESULT (WINAPI *GetReconversion)(ITfFnReconversionLocal *, ITfRange *, ITfCandidateListLocal **);
    HRESULT (WINAPI *Reconvert)(ITfFnReconversionLocal *, ITfRange *);
} ITfFnReconversionLocalVtbl;
struct ITfFnReconversionLocal { const ITfFnReconversionLocalVtbl *lpVtbl; };

typedef struct ITfCandidateStringLocalVtbl {
    HRESULT (WINAPI *QueryInterface)(ITfCandidateStringLocal *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ITfCandidateStringLocal *);
    ULONG (WINAPI *Release)(ITfCandidateStringLocal *);
    /* ctffunc.h order is GetString THEN GetIndex (not the other way). */
    HRESULT (WINAPI *GetString)(ITfCandidateStringLocal *, BSTR *);
    HRESULT (WINAPI *GetIndex)(ITfCandidateStringLocal *, ULONG *);
} ITfCandidateStringLocalVtbl;
struct ITfCandidateStringLocal { const ITfCandidateStringLocalVtbl *lpVtbl; };

typedef struct ITfCandidateListLocalVtbl {
    HRESULT (WINAPI *QueryInterface)(ITfCandidateListLocal *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ITfCandidateListLocal *);
    ULONG (WINAPI *Release)(ITfCandidateListLocal *);
    HRESULT (WINAPI *EnumCandidates)(ITfCandidateListLocal *, void **);
    HRESULT (WINAPI *GetCandidate)(ITfCandidateListLocal *, ULONG, ITfCandidateStringLocal **);
    HRESULT (WINAPI *GetCandidateNum)(ITfCandidateListLocal *, ULONG *);
    HRESULT (WINAPI *SetResult)(ITfCandidateListLocal *, ULONG, int);
} ITfCandidateListLocalVtbl;
struct ITfCandidateListLocal { const ITfCandidateListLocalVtbl *lpVtbl; };

/* ITfFnSearchCandidateProvider : ITfFunction — GetSearchCandidates returns an
 * ITfCandidateList for a query string directly (no range / edit session), which
 * is the modore-shaped "candidates for this reading" call. (Declared here, after
 * ITfCandidateListLocal, which its signatures reference.) */
typedef struct ITfFnSearchCandidateProviderLocal ITfFnSearchCandidateProviderLocal;
typedef struct ITfFnSearchCandidateProviderLocalVtbl {
    HRESULT (WINAPI *QueryInterface)(ITfFnSearchCandidateProviderLocal *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ITfFnSearchCandidateProviderLocal *);
    ULONG (WINAPI *Release)(ITfFnSearchCandidateProviderLocal *);
    HRESULT (WINAPI *GetDisplayName)(ITfFnSearchCandidateProviderLocal *, BSTR *);   /* ITfFunction */
    HRESULT (WINAPI *GetSearchCandidates)(ITfFnSearchCandidateProviderLocal *, BSTR, BSTR, ITfCandidateListLocal **);
    HRESULT (WINAPI *SetResult)(ITfFnSearchCandidateProviderLocal *, BSTR, BSTR, BSTR);
} ITfFnSearchCandidateProviderLocalVtbl;
struct ITfFnSearchCandidateProviderLocal { const ITfFnSearchCandidateProviderLocalVtbl *lpVtbl; };

/* Log a (possibly bad) BSTR using its length prefix rather than scanning for a
 * NUL — same defensive read used for candidate strings. */
static void log_bstr_value(const char *prefix, BSTR s)
{
    UINT blen;
    char line[512];
    int n;
    if (!s) { log_str(prefix); log_str(" <null>\r\n"); return; }
    blen = ((UINT *)s)[-1] / 2;
    if (blen > 200) blen = 200;
    n = WideCharToMultiByte(CP_UTF8, 0, s, (int)blen, line, (int)sizeof(line) - 1, 0, 0);
    log_str(prefix);
    if (n > 0) { line[n] = 0; log_str(line); }
    log_str("\r\n");
}

/* Map ATOK's TSF function-provider surface: for each well-known function IID,
 * ask the provider GetFunction(GUID_NULL, iid) and, on success, read the
 * function's display name (ITfFunction slot 3). This enumerates what ATOK
 * actually exposes beyond reconversion — notably whether
 * ITfFnSearchCandidateProvider (a direct query->candidate-list call) is
 * available, which would be a cleaner bridge surface than commit+reconvert. */
void probe_function_provider(ITfFunctionProvider *prov)
{
    static const struct { const char *name; const GUID *iid; } funcs[] = {
        { "ITfFnReconversion",            &IID_LOCAL_ITfFnReconversion },
        { "ITfFnSearchCandidateProvider", &IID_LOCAL_ITfFnSearchCandidateProvider },
        { "ITfFnConfigure",               &IID_LOCAL_ITfFnConfigure },
        { "ITfFnConfigureRegisterWord",   &IID_LOCAL_ITfFnConfigureRegisterWord },
        { "ITfFnLangProfileUtil",         &IID_LOCAL_ITfFnLangProfileUtil },
    };
    char buf[256];
    unsigned i;

    /* Provider's own type/description, for context. */
    {
        BSTR pd = 0;
        if (prov->lpVtbl->GetDescription(prov, &pd) == S_OK)
            log_bstr_value("FUNCPROV provider description= ", pd);
    }
    for (i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++) {
        IUnknown *f = 0;
        HRESULT hr = prov->lpVtbl->GetFunction(prov, &GUID_LOCAL_NULL,
                                               (REFIID)funcs[i].iid, &f);
        wsprintfA(buf, "FUNCPROV %-30s hr=0x%08lX f=%p\r\n",
                  funcs[i].name, (unsigned long)hr, f);
        log_str(buf);
        if (hr == S_OK && f) {
            ITfFunctionLocal *fn = (ITfFunctionLocal *)f;
            BSTR dn = 0;
            if (fn->lpVtbl->GetDisplayName(fn, &dn) == S_OK)
                log_bstr_value("  displayName= ", dn);
            f->lpVtbl->Release(f);
        }
    }
}

/* Hand-rolled BSTR (the harness is freestanding — no oleaut32/SysAllocString).
 * Layout: [UINT byteLen][WCHARs...][NUL]; the returned pointer is at the chars,
 * and the 4-byte length prefix lets callees use SysStringLen. Only ever passed
 * as an [in] param, so the callee never frees it; we free it ourselves. */
static BSTR make_bstr(const WCHAR *s)
{
    UINT n = 0, bytes;
    char *base;
    while (s && s[n]) n++;
    bytes = n * 2;
    base = (char *)HeapAlloc(GetProcessHeap(), 0, bytes + 6);
    if (!base) return 0;
    *(UINT *)base = bytes;
    if (bytes) CopyMemory(base + 4, s, bytes);
    *(WCHAR *)(base + 4 + bytes) = 0;
    return (BSTR)(base + 4);
}
static void free_bstr(BSTR b) { if (b) HeapFree(GetProcessHeap(), 0, (char *)b - 4); }

/* Enumerate + log an ITfCandidateList (count, then per-index index/value).
 * GetCandidate returns the list's shared ITfCandidateString singleton (same
 * pointer each call) — do NOT Release per iteration. Returns the count. */
static ULONG dump_candidate_strings(ITfCandidateListLocal *cands, const char *tag)
{
    ULONG num = 0, i;
    char buf[256];
    HRESULT hr = cands->lpVtbl->GetCandidateNum(cands, &num);
    wsprintfA(buf, "CANDLIST [%s] count=%lu (hr=0x%08lX)\r\n",
              tag, (unsigned long)num, (unsigned long)hr);
    log_str(buf);
    for (i = 0; i < num && i < 64; i++) {
        ITfCandidateStringLocal *cs = 0;
        HRESULT hcc = cands->lpVtbl->GetCandidate(cands, i, &cs);
        if (hcc == S_OK && cs) {
            ULONG idx = 0xFFFFFFFF;
            BSTR s = 0;
            HRESULT hgi = cs->lpVtbl->GetIndex(cs, &idx);
            HRESULT hgs = cs->lpVtbl->GetString(cs, &s);
            wsprintfA(buf, "  cand[%lu] idxHr=0x%08lX idx=%lu strHr=0x%08lX s=%p\r\n",
                      (unsigned long)i, (unsigned long)hgi, (unsigned long)idx,
                      (unsigned long)hgs, s);
            log_str(buf);
            if (hgs == S_OK && s) log_bstr_value("    value= ", s);
        } else {
            wsprintfA(buf, "  GetCandidate[%lu] hr=0x%08lX\r\n",
                      (unsigned long)i, (unsigned long)hcc);
            log_str(buf);
        }
    }
    return num;
}

/* Call ATOK's ITfFnSearchCandidateProvider::GetSearchCandidates(query) — a
 * direct reading->candidate-list call that needs no edit session, no commit,
 * no reconversion range. Gated by AT_SEARCH_QUERY (the query string, UTF-8 in
 * the env, e.g. きしゃ). This evaluates whether the search-provider surface can
 * replace the commit+reconvert flow for the bridge. AT_SEARCH_APPID overrides
 * the application-id arg (default empty). */
void probe_search_candidates(void)
{
    ITfFunctionProvider *prov = 0;
    IUnknown *unk = 0;
    ITfFnSearchCandidateProviderLocal *scp = 0;
    ITfCandidateListLocal *cands = 0;
    WCHAR query[128], appid[128];
    BSTR bq = 0, ba = 0;
    HRESULT hr;
    char buf[256];

    if (GetEnvironmentVariableW(L"AT_SEARCH_QUERY", query,
                                (DWORD)(sizeof(query) / sizeof(query[0]))) == 0)
        return;
    if (GetEnvironmentVariableW(L"AT_SEARCH_APPID", appid,
                                (DWORD)(sizeof(appid) / sizeof(appid[0]))) == 0)
        appid[0] = 0;

    hr = g_tm->lpVtbl->GetFunctionProvider(g_tm, &CLSID_ATOK_TIP, &prov);
    wsprintfA(buf, "SEARCH GetFunctionProvider hr=0x%08lX\r\n", (unsigned long)hr);
    log_str(buf);
    if (hr != S_OK || !prov) return;

    hr = prov->lpVtbl->GetFunction(prov, &GUID_LOCAL_NULL,
                                   &IID_LOCAL_ITfFnSearchCandidateProvider, &unk);
    wsprintfA(buf, "SEARCH GetFunction(SearchCandidateProvider) hr=0x%08lX unk=%p\r\n",
              (unsigned long)hr, unk);
    log_str(buf);
    if (hr == S_OK && unk) {
        unk->lpVtbl->QueryInterface(unk, &IID_LOCAL_ITfFnSearchCandidateProvider, (void **)&scp);
        unk->lpVtbl->Release(unk);
    }
    if (!scp) { prov->lpVtbl->Release(prov); return; }

    log_bstr_value("SEARCH query= ", (bq = make_bstr(query)));
    ba = make_bstr(appid);
    hr = scp->lpVtbl->GetSearchCandidates(scp, bq, ba, &cands);
    wsprintfA(buf, "SEARCH GetSearchCandidates hr=0x%08lX cands=%p\r\n",
              (unsigned long)hr, cands);
    log_str(buf);
    if (hr == S_OK && cands) {
        dump_candidate_strings(cands, "search");
        cands->lpVtbl->Release(cands);
    }
    free_bstr(bq);
    free_bstr(ba);
    scp->lpVtbl->Release(scp);
    prov->lpVtbl->Release(prov);

    if (env_flag_enabled(L"AT_SEARCH_EXIT")) {
        log_str("SEARCH done; clean exit\r\n");
        ExitProcess(0);
    }
}

/* Daemon fast path: pull a candidate list straight from
 * ITfFnSearchCandidateProvider::GetSearchCandidates(query). No edit session, no
 * commit, no reconversion teardown — so it is both faster than the reconversion
 * path and immune to the wine msctf teardown AV. `query` is the reading. */
void search_candidates_daemon(const WCHAR *query)
{
    ITfFunctionProvider *prov = 0;
    IUnknown *unk = 0;
    ITfFnSearchCandidateProviderLocal *scp = 0;
    ITfCandidateListLocal *cands = 0;
    BSTR bq = 0, ba = 0;
    HRESULT hr;
    char buf[256];
    ULONG num = 0, i;

    at_daemon_ms("srch_enter");
    hr = g_tm->lpVtbl->GetFunctionProvider(g_tm, &CLSID_ATOK_TIP, &prov);
    if (hr != S_OK || !prov) return;
    hr = prov->lpVtbl->GetFunction(prov, &GUID_LOCAL_NULL,
                                   &IID_LOCAL_ITfFnSearchCandidateProvider, &unk);
    if (hr == S_OK && unk) {
        unk->lpVtbl->QueryInterface(unk, &IID_LOCAL_ITfFnSearchCandidateProvider,
                                    (void **)&scp);
        unk->lpVtbl->Release(unk);
    }
    if (!scp) { prov->lpVtbl->Release(prov); return; }
    at_daemon_ms("srch_fn");

    {
        char dq[256];
        WCHAR appid[128];
        int dn = WideCharToMultiByte(CP_UTF8, 0, query, -1, dq, (int)sizeof(dq) - 1, 0, 0);
        if (dn > 0) { dq[dn] = 0; log_str("SEARCH query= "); log_str(dq); log_str("\r\n"); }
        bq = make_bstr(query);
        /* AT_SEARCH_APPID lets us probe whether ATOK needs a recognized
         * application id to return real predictions (empty = placeholder?). */
        if (GetEnvironmentVariableW(L"AT_SEARCH_APPID", appid,
                                    (DWORD)(sizeof(appid) / sizeof(appid[0]))) > 0) {
            ba = make_bstr(appid);
            log_str("SEARCH appid set\r\n");
        } else {
            ba = make_bstr(L"");
        }
    }
    hr = scp->lpVtbl->GetSearchCandidates(scp, bq, ba, &cands);
    at_daemon_ms("srch_get");
    wsprintfA(buf, "SEARCH GetSearchCandidates hr=0x%08lX cands=%p\r\n",
              (unsigned long)hr, cands);
    log_str(buf);
    if (hr == S_OK && cands) {
        cands->lpVtbl->GetCandidateNum(cands, &num);
        wsprintfA(buf, "SEARCH candidate count=%lu\r\n", (unsigned long)num);
        log_str(buf);
        for (i = 0; i < num && i < 64; i++) {
            ITfCandidateStringLocal *cs = 0;
            if (cands->lpVtbl->GetCandidate(cands, i, &cs) == S_OK && cs) {
                BSTR s = 0;
                ULONG idx = 0xFFFFFFFF;
                HRESULT hgi = cs->lpVtbl->GetIndex(cs, &idx);
                if (cs->lpVtbl->GetString(cs, &s) == S_OK && s) {
                    /* same shared-singleton + BSTR-length-prefix rules as the
                     * reconversion enumerator; do NOT Release cs per iteration */
                    UINT blen = ((UINT *)s)[-1] / 2;
                    char line[512];
                    int n, k, kk;
                    char hexb[160];
                    if (blen > 200) blen = 200;
                    /* raw UTF-16 hex — confirms whether '?' candidates are real
                     * U+003F or an extraction artifact. */
                    kk = 0;
                    for (k = 0; k < (int)blen && k < 12; k++)
                        kk += wsprintfA(hexb + kk, "%04X ", (unsigned)((WCHAR *)s)[k]);
                    wsprintfA(buf, "  scand[%lu] idxHr=0x%08lX idx=%lu blen=%u u16= %s\r\n",
                              (unsigned long)i, (unsigned long)hgi, (unsigned long)idx,
                              blen, hexb);
                    log_str(buf);
                    n = WideCharToMultiByte(CP_UTF8, 0, s, (int)blen, line,
                                            (int)sizeof(line) - 1, 0, 0);
                    if (n > 0) {
                        line[n] = 0; log_str("    value= "); log_str(line); log_str("\r\n");
                        if (g_daemon_cap_active && g_daemon_cap_count < AT_CAP_MAX) {
                            lstrcpynA(g_daemon_cap[g_daemon_cap_count], line, AT_CAP_BYTES);
                            g_daemon_cap_count++;
                        }
                    }
                }
            }
        }
        cands->lpVtbl->Release(cands);
    }
    at_daemon_ms("srch_enum");
    if (g_daemon_cap_active) at_daemon_emit_result();
    free_bstr(bq);
    free_bstr(ba);
    scp->lpVtbl->Release(scp);
    prov->lpVtbl->Release(prov);
}

/* --- candidate-metadata memory probe (AT_SCAN_META) ----------------------
 * ITfCandidateString narrows each candidate to {string,index}; AT's engine
 * word-info record (CTangoInfoEditArray@de@atok) also carries a `comment`
 * (description) field that the TSF boundary drops. While the reconversion
 * candidate list is still alive in-process, scan the heap for a candidate's
 * UTF-16 surface string and dump the surrounding bytes; also scan for 4-byte
 * back-references (records that *point* to the string) so we catch the record
 * whether the surface is stored inline or by pointer. Read-only recon, gated
 * AT_SCAN_META. See docs/at-candidate-metadata-recon.md. */

/* Dump bytes [hit-before .. hit+after), 16/row: addr, hex, UTF-16LE rendering. */
static void log_mem_window(const BYTE *hit, const BYTE *lo, const BYTE *hi,
                           int before, int after)
{
    const BYTE *start = (hit - lo > before) ? hit - before : lo;
    const BYTE *end = hit + after;
    const BYTE *p;
    if (end > hi) end = hi;
    for (p = start; p < end; p += 16) {
        char line[256], txt[80];
        int li = 0, ti = 0, k;
        li += wsprintfA(line + li, "    %08lX%s ",
                        (unsigned long)(UINT_PTR)p,
                        (p <= hit && hit < p + 16) ? "*" : " ");
        for (k = 0; k < 16; k++)
            li += (p + k < end) ? wsprintfA(line + li, "%02X ", p[k])
                                : wsprintfA(line + li, "   ");
        for (k = 0; k + 1 < 16 && p + k + 1 < end; k += 2) {
            WCHAR w = (WCHAR)(p[k] | (p[k + 1] << 8));
            char u[8];
            int n = (w >= 0x20 && w != 0x7f)
                        ? WideCharToMultiByte(CP_UTF8, 0, &w, 1, u, sizeof(u) - 1, 0, 0)
                        : 0;
            if (n > 0) { u[n] = 0; ti += wsprintfA(txt + ti, "%s", u); }
            else txt[ti++] = '.';
        }
        txt[ti] = 0;
        wsprintfA(line + li, " | %s\r\n", txt);
        log_str(line);
    }
}

/* True if a region is committed, readable heap (MEM_PRIVATE) and not huge. */
static BOOL scan_region_ok(const MEMORY_BASIC_INFORMATION *mbi)
{
    DWORD r = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return mbi->State == MEM_COMMIT && mbi->Type == MEM_PRIVATE &&
           (mbi->Protect & r) && !(mbi->Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           mbi->RegionSize <= 0x4000000;
}

/* Scan heap for the dword value `needle` (a back-pointer to a string we found)
 * and dump a window around each hit — those are candidate record structures. */
#define SCAN_VM_LO ((BYTE *)0x00010000)
#define SCAN_VM_HI ((BYTE *)0x7FFE0000)

static void scan_dword_refs(UINT_PTR needle, int max)
{
    BYTE *addr = SCAN_VM_LO;
    int hits = 0;
    char buf[160];
    log_str("  SCAN_META back-refs (pointers to the string = record bodies):\r\n");
    while (addr < SCAN_VM_HI && hits < max) {
        MEMORY_BASIC_INFORMATION mbi;
        BYTE *next;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        next = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
        if (scan_region_ok(&mbi)) {
            BYTE *p = (BYTE *)mbi.BaseAddress;
            BYTE *rend = next - 4;
            for (; p <= rend && hits < max; p += 4) {
                if (*(UINT_PTR *)p != needle) continue;
                wsprintfA(buf, "  ref #%d @ %08lX\r\n", hits, (unsigned long)(UINT_PTR)p);
                log_str(buf);
                log_mem_window(p, (BYTE *)mbi.BaseAddress, next, 0x10, 0x60);
                hits++;
            }
        }
        if (next <= addr) break;
        addr = next;
    }
    wsprintfA(buf, "  SCAN_META back-refs done: %d\r\n", hits);
    log_str(buf);
}

static void scan_candidate_metadata(ITfCandidateListLocal *cands, ULONG num)
{
    WCHAR target[64];
    int tlen = 0;
    DWORD idx = get_env_dword(L"AT_SCAN_META_IDX", 0xFFFFFFFF);
    int max_hits = (int)get_env_dword(L"AT_SCAN_META_HITS", 16);
    ULONG i;
    BYTE *addr = SCAN_VM_LO;
    int total = 0;
    UINT_PTR first_hit = 0;
    char buf[160];

    /* target = explicit AT_SCAN_META_IDX, else first candidate with >=2 chars */
    for (i = 0; i < num; i++) {
        ITfCandidateStringLocal *cs = 0;
        BSTR s = 0;
        if (cands->lpVtbl->GetCandidate(cands, i, &cs) != S_OK || !cs) continue;
        if (cs->lpVtbl->GetString(cs, &s) == S_OK && s) {
            UINT blen = ((UINT *)s)[-1] / 2;
            if (((idx != 0xFFFFFFFF && i == idx) ||
                 (idx == 0xFFFFFFFF && blen >= 2 && tlen == 0)) &&
                blen > 0 && blen < 63) {
                UINT j;
                for (j = 0; j < blen; j++) target[j] = s[j];
                target[blen] = 0;
                tlen = (int)blen;
            }
        }
        if (idx != 0xFFFFFFFF && i == idx) break;
    }
    if (tlen == 0) { log_str("SCAN_META: no usable target candidate\r\n"); return; }
    {
        char u[128];
        int n = WideCharToMultiByte(CP_UTF8, 0, target, tlen, u, sizeof(u) - 1, 0, 0);
        u[n > 0 ? n : 0] = 0;
        wsprintfA(buf, "SCAN_META target='%s' cch=%d — scanning heap\r\n", u, tlen);
        log_str(buf);
    }

    {
    int win = (int)get_env_dword(L"AT_SCAN_META_WIN", 0x160);
    int bare = 0, shown = 0;
    while (addr < SCAN_VM_HI && total < max_hits) {
        MEMORY_BASIC_INFORMATION mbi;
        BYTE *next;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        next = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
        if (scan_region_ok(&mbi)) {
            BYTE b0 = (BYTE)(target[0] & 0xFF), b1 = (BYTE)((target[0] >> 8) & 0xFF);
            BYTE *p = (BYTE *)mbi.BaseAddress;
            BYTE *rend = next - tlen * 2;
            for (; p <= rend && total < max_hits; p++) {
                int j, eq = 1, interesting = 0;
                const BYTE *q, *qend;
                if (p[0] != b0 || p[1] != b1) continue;
                for (j = 1; j < tlen; j++)
                    if (*(const WCHAR *)(p + j * 2) != target[j]) { eq = 0; break; }
                if (!eq) continue;
                total++;
                /* "interesting" = non-zero bytes follow the string (a record /
                 * display table), vs a bare zero-padded pooled copy. */
                qend = p + tlen * 2 + 0x40;
                if (qend > next) qend = next;
                for (q = p + tlen * 2; q < qend; q++)
                    if (*q) { interesting = 1; break; }
                if (!interesting) { bare++; continue; }
                wsprintfA(buf, "SCAN_META hit #%d @ %08lX (region %08lX prot=%lX) [structured]\r\n",
                          total - 1, (unsigned long)(UINT_PTR)p,
                          (unsigned long)(UINT_PTR)mbi.BaseAddress,
                          (unsigned long)mbi.Protect);
                log_str(buf);
                log_mem_window(p, (BYTE *)mbi.BaseAddress, next, 0x20, win);
                shown++;
                /* back-ref chase a heap record, not a stack copy */
                if (!first_hit && (UINT_PTR)p >= 0x00400000) first_hit = (UINT_PTR)p;
            }
        }
        if (next <= addr) break;
        addr = next;
    }
    wsprintfA(buf, "SCAN_META hits=%d structured=%d bare=%d\r\n", total, shown, bare);
    log_str(buf);
    }
    if (first_hit)
        scan_dword_refs(first_hit, (int)get_env_dword(L"AT_SCAN_META_REFS", 12));
    scan_heap_comment_markers("reconv");
}

/* Target-agnostic: scan the heap for ATOK annotation/comment marker chars
 * (〔【〈《▼※) that are followed by a CJK run — i.e. a real 注釈/使い分け
 * string, not stray punctuation. Catches a comment wherever it loads, without
 * needing to know which candidate it belongs to. Used to test whether forcing
 * the candidate-info window populates comments. */
/* True if `p` begins a plausible NUL-terminated text string (2..48 "texty"
 * wchars then U+0000) — filters dense numeric/index tables whose 16-bit values
 * coincidentally equal marker codepoints (they never NUL-terminate). */
static int looks_like_string(const BYTE *p, const BYTE *next)
{
    int n = 0;
    const BYTE *q = p;
    for (; q + 1 < next && n < 64; q += 2, n++) {
        WCHAR w = (WCHAR)(q[0] | (q[1] << 8));
        if (w == 0) return n >= 2;
        /* texty = ASCII printable, JP punctuation/kana, or CJK ideographs */
        if (!((w >= 0x20 && w < 0x7f) || (w >= 0x3000 && w <= 0x30ff) ||
              (w >= 0x4e00 && w <= 0x9fff) || (w >= 0xff00 && w <= 0xffef)))
            return 0;
    }
    return 0;
}

void scan_heap_comment_markers(const char *tag)
{
    static const WCHAR markers[] = { 0x3014, 0x3010, 0x3008, 0x300A, 0x25BC, 0x203B };
    int win = (int)get_env_dword(L"AT_SCAN_META_WIN", 0x120);
    int max_hits = (int)get_env_dword(L"AT_MARK_HITS", 40);
    int nm = (int)(sizeof(markers) / sizeof(markers[0]));
    int total = 0;
    BYTE *addr = SCAN_VM_LO;
    char buf[160];
    wsprintfA(buf, "SCAN_MARK [%s]: heap scan for annotation markers 〔【〈《▼※\r\n", tag);
    log_str(buf);
    while (addr < SCAN_VM_HI && total < max_hits) {
        MEMORY_BASIC_INFORMATION mbi;
        BYTE *next;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        next = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
        if (scan_region_ok(&mbi)) {
            BYTE *p = (BYTE *)mbi.BaseAddress;
            BYTE *rend = next - 2;
            for (; p <= rend && total < max_hits; p += 2) {
                WCHAR w = (WCHAR)(p[0] | (p[1] << 8));
                int k, m = 0;
                for (k = 0; k < nm; k++) if (w == markers[k]) { m = 1; break; }
                if (!m) continue;
                if (!looks_like_string(p, next)) continue;
                wsprintfA(buf, "SCAN_MARK hit #%d @ %08lX U+%04X\r\n",
                          total, (unsigned long)(UINT_PTR)p, (unsigned)w);
                log_str(buf);
                log_mem_window(p, (BYTE *)mbi.BaseAddress, next, 0x10, win);
                total++;
            }
        }
        if (next <= addr) break;
        addr = next;
    }
    wsprintfA(buf, "SCAN_MARK [%s] done: %d hit(s)\r\n", tag, total);
    log_str(buf);
}

/* Pull ATOK's candidate list programmatically via the TSF reconversion
 * function. ATOK registers an ITfFunctionProvider; GetFunction(GUID_NULL,
 * ITfFnReconversion) yields the reconversion function, and GetReconversion(range)
 * returns an ITfCandidateList for the text in `range` — the full candidate set,
 * without needing ATOK's candidate window to open. This is the modore-shaped
 * "give me the candidates for this text" call. Gated by AT_PROBE_RECONV. */
/* Daemon-mode candidate capture. Filled by the enumerator below when active;
 * read back by the `convert` runtime command (AtTipLoad.c) to emit a structured
 * result block. Declared in at_runtime.h. */
int  g_daemon_cap_active = 0;
int  g_daemon_cap_count = 0;
char g_daemon_cap[AT_CAP_MAX][AT_CAP_BYTES];

/* Core reconversion, run with a valid edit cookie `ec` (a real document lock).
 * SetSelection and ATOK's QueryRange need a lock — outside one they fail with
 * TS_E_NOLOCK / E_INVALIDARG, so this must run inside an edit session. */
static void do_reconversion(ITfContext *context, TfEditCookie ec, const char *tag)
{
    ITfFunctionProvider *prov = 0;
    IUnknown *unk = 0;
    ITfFnReconversionLocal *recon = 0;
    ITfCandidateListLocal *cands = 0;
    ITfRange *range = 0, *newRange = 0;
    BOOL convertable = FALSE;
    ULONG num = 0, i;
    HRESULT hr;
    BSTR dn = 0;
    char buf[256];

    at_daemon_ms("recon_enter");
    hr = g_tm->lpVtbl->GetFunctionProvider(g_tm, &CLSID_ATOK_TIP, &prov);
    wsprintfA(buf, "RECONV [%s] GetFunctionProvider hr=0x%08lX\r\n", tag, (unsigned long)hr);
    log_str(buf);
    if (hr != S_OK || !prov) return;
    at_daemon_ms("recon_fp");

    if (env_flag_enabled(L"AT_PROBE_FUNCS"))
        probe_function_provider(prov);

    hr = prov->lpVtbl->GetFunction(prov, &GUID_LOCAL_NULL,
                                   &IID_LOCAL_ITfFnReconversion, &unk);
    wsprintfA(buf, "RECONV GetFunction(ITfFnReconversion) hr=0x%08lX unk=%p\r\n",
              (unsigned long)hr, unk);
    log_str(buf);
    if (hr == S_OK && unk) {
        unk->lpVtbl->QueryInterface(unk, &IID_LOCAL_ITfFnReconversion, (void **)&recon);
        unk->lpVtbl->Release(unk);
    }
    if (!recon) { prov->lpVtbl->Release(prov); return; }
    at_daemon_ms("recon_fn");

    if (recon->lpVtbl->GetDisplayName(recon, &dn) == S_OK && dn) {
        char dl[256];
        int n = WideCharToMultiByte(CP_UTF8, 0, dn, -1, dl, (int)sizeof(dl) - 1, 0, 0);
        log_str("RECONV displayName= ");
        if (n > 0) log_str(dl);
        log_str("\r\n");
    }

    hr = context->lpVtbl->GetStart(context, ec, &range);
    wsprintfA(buf, "RECONV GetStart hr=0x%08lX range=%p\r\n", (unsigned long)hr, range);
    log_str(buf);
    if (range) {
        LONG shifted = 0;
        BOOL empty = TRUE;
        WCHAR rt[64]; ULONG got = 0;
        range->lpVtbl->ShiftEnd(range, ec, 64, &shifted, 0);
        if (range->lpVtbl->GetText(range, ec, 0, rt, 63, &got) == S_OK) {
            char line[256];
            int n;
            log_str("RECONV range text= ");
            n = WideCharToMultiByte(CP_UTF8, 0, rt, (int)got, line, (int)sizeof(line) - 1, 0, 0);
            if (n > 0) { line[n] = 0; log_str(line); }
            log_str("\r\n");
        }
        range->lpVtbl->Collapse(range, ec, 0 /*TF_ANCHOR_START*/);
        range->lpVtbl->ShiftEnd(range, ec, (LONG)got, &shifted, 0);
        if (range->lpVtbl->IsEmpty(range, ec, &empty) == S_OK) {
            wsprintfA(buf, "RECONV range clamped to %ld empty=%d\r\n", (long)got, empty);
            log_str(buf);
        }
        /* select the span (now succeeds — we hold a read/write lock) so ATOK's
         * QueryRange sees a non-empty current selection. */
        {
            TF_SELECTION tsel;
            ZeroMemory(&tsel, sizeof(tsel));
            tsel.range = range;
            tsel.style.ase = TF_AE_END;
            tsel.style.fInterimChar = FALSE;
            hr = context->lpVtbl->SetSelection(context, ec, 1, &tsel);
            wsprintfA(buf, "RECONV SetSelection hr=0x%08lX\r\n", (unsigned long)hr);
            log_str(buf);
        }
        at_daemon_ms("recon_pre_qr");
        /* QueryRange asks ATOK for the convertable sub-range; it costs a full IPC
         * round-trip (~80ms) and for a freshly-committed reading just returns the
         * whole range. AT_SKIP_QUERYRANGE feeds `range` straight to GetReconversion
         * and skips it. (Kept on by default until proven safe across word shapes.) */
        if (!env_flag_enabled(L"AT_SKIP_QUERYRANGE")) {
            hr = recon->lpVtbl->QueryRange(recon, range, &newRange, &convertable);
            wsprintfA(buf, "RECONV QueryRange hr=0x%08lX convertable=%d newRange=%p\r\n",
                      (unsigned long)hr, convertable, newRange);
            log_str(buf);
        }
        at_daemon_ms("recon_qr");
        hr = recon->lpVtbl->GetReconversion(recon, newRange ? newRange : range, &cands);
        wsprintfA(buf, "RECONV GetReconversion hr=0x%08lX cands=%p\r\n",
                  (unsigned long)hr, cands);
        log_str(buf);
    }
    at_daemon_ms("recon_list");
    if (cands) {
        hr = cands->lpVtbl->GetCandidateNum(cands, &num);
        wsprintfA(buf, "RECONV candidate count=%lu (hr=0x%08lX)\r\n",
                  (unsigned long)num, (unsigned long)hr);
        log_str(buf);
        for (i = 0; i < num && i < 64; i++) {
            ITfCandidateStringLocal *cs = 0;
            HRESULT hcc = cands->lpVtbl->GetCandidate(cands, i, &cs);
            wsprintfA(buf, "  reconv GetCandidate[%lu] hr=0x%08lX cs=%p\r\n",
                      (unsigned long)i, (unsigned long)hcc, cs);
            log_str(buf);
            if (hcc == S_OK && cs) {
                ULONG idx = 0xFFFFFFFF;
                BSTR s = 0;
                HRESULT hgi = cs->lpVtbl->GetIndex(cs, &idx);
                HRESULT hgs = cs->lpVtbl->GetString(cs, &s);
                /* Log the raw pointer + hrs BEFORE dereferencing s, so a bad
                 * BSTR shows up as data rather than a page fault. */
                wsprintfA(buf, "  reconv cand[%lu] idxHr=0x%08lX idx=%lu strHr=0x%08lX s=%p\r\n",
                          (unsigned long)i, (unsigned long)hgi, (unsigned long)idx,
                          (unsigned long)hgs, s);
                log_str(buf);
                if (hgs == S_OK && s) {
                    /* Use the BSTR length prefix (UINT at s[-1] in bytes) to bound
                     * the read instead of scanning for a NUL on a possibly-bad
                     * pointer. */
                    UINT blen = ((UINT *)s)[-1] / 2;
                    char line[512];
                    int n;
                    if (blen > 200) blen = 200;
                    n = WideCharToMultiByte(CP_UTF8, 0, s, (int)blen, line, (int)sizeof(line) - 1, 0, 0);
                    if (n > 0) {
                        line[n] = 0; log_str("    value= "); log_str(line); log_str("\r\n");
                        if (g_daemon_cap_active && g_daemon_cap_count < AT_CAP_MAX) {
                            lstrcpynA(g_daemon_cap[g_daemon_cap_count], line, AT_CAP_BYTES);
                            g_daemon_cap_count++;
                        }
                    }
                }
                /* GetCandidate returns the list's shared ITfCandidateString
                 * singleton (same pointer for every index), so do NOT Release it
                 * per-iteration — that over-releases and faults in teardown. */
            }
        }
        at_daemon_ms("recon_enum");
        /* Flush the structured result to the relay now — before SetResult and
         * the edit-session teardown, which can fault on a long-lived process. */
        if (g_daemon_cap_active) at_daemon_emit_result();
        if (env_flag_enabled(L"AT_SCAN_META"))
            scan_candidate_metadata(cands, num);
        /* Daemon mode: the candidates are captured AND already emitted to the
         * relay. Everything left here (SetResult, the COM releases, and the
         * edit-session teardown ATOK runs on top of them) is the ~6s lock-wait /
         * wine-11.5 crash and never returns cleanly. Since the relay uses a fresh
         * single-shot TIP per convert, just exit now — clean (exit 0, not a crash
         * or SIGKILL), so the shared servers/CE are left in a consistent state for
         * the next TIP. Gate AT_RECONV_FORCESET keeps the full SetResult path. */
        if (g_daemon_cap_active &&
            !env_flag_enabled(L"AT_RECONV_FORCESET") &&
            !env_flag_enabled(L"AT_RECONV_KEEPALIVE")) {
            log_str("  reconv daemon: skip teardown, clean exit\r\n");
            ExitProcess(0);
        }
        /* KEEPALIVE experiment: keep the process warm for the next convert WITHOUT
         * SetResult (which deadlocks under Wine — its nested edit-session request
         * blocks on the sync lock we already hold). Just release the list and let
         * the edit session unwind; see whether ATOK tolerates the abandoned
         * reconversion or faults/garbles the next convert. */
        if (env_flag_enabled(L"AT_RECONV_KEEPALIVE")) {
            log_str("  reconv daemon: KEEPALIVE — release list, no SetResult\r\n");
            cands->lpVtbl->Release(cands);
            goto reconv_cleanup;
        }
        /* TSF protocol: a reconversion candidate list MUST be resolved with
         * SetResult, else ATOK's reconversion session is left dangling and
         * faults during edit-session teardown. CAND_CANCELED(0) cleanly abandons
         * (read-only probe); CAND_SELECTED(1)/CAND_FINALIZED(2) on a chosen index
         * is the select-by-index path for later. AT_RECONV_SELECT=<n> picks. */
        {
            DWORD sel = (g_reconv_select != 0xFFFFFFFF)
                            ? g_reconv_select
                            : get_env_dword(L"AT_RECONV_SELECT", 0xFFFFFFFF);
            BOOL did_select = (sel != 0xFFFFFFFF && sel < num);
            HRESULT hsr;
            if (did_select)
                hsr = cands->lpVtbl->SetResult(cands, sel, 1 /*CAND_SELECTED*/);
            else
                hsr = cands->lpVtbl->SetResult(cands, 0, 0 /*CAND_CANCELED*/);
            wsprintfA(buf, "  reconv SetResult sel=%ld hr=0x%08lX\r\n",
                      did_select ? (long)sel : -1L, (unsigned long)hsr);
            log_str(buf);
            at_daemon_ms("recon_setresult");
            /* Commit-back verification: after selecting a candidate, re-read the
             * whole document. If ATOK wrote the chosen candidate back over the
             * reading via the reconversion range, the doc text now shows it —
             * proving SetResult(CAND_SELECTED) IS the select-by-index commit the
             * bridge needs. If the reading is unchanged, the select needs a
             * further finalize step (CAND_FINALIZED / an explicit Reconvert). */
            if (did_select) {
                ITfRange *vr = 0;
                if (context->lpVtbl->GetStart(context, ec, &vr) == S_OK && vr) {
                    LONG sh = 0; ULONG vgot = 0; WCHAR vt[128];
                    vr->lpVtbl->ShiftEnd(vr, ec, 127, &sh, 0);
                    if (vr->lpVtbl->GetText(vr, ec, 0, vt, 127, &vgot) == S_OK) {
                        char line[512]; int n;
                        log_str("RECONV post-select doc text= ");
                        n = WideCharToMultiByte(CP_UTF8, 0, vt, (int)vgot, line,
                                                (int)sizeof(line) - 1, 0, 0);
                        if (n > 0) { line[n] = 0; log_str(line); }
                        log_str("\r\n");
                    }
                    vr->lpVtbl->Release(vr);
                }
            }
        }
        cands->lpVtbl->Release(cands);
        /* Clean exit ONLY after a successful extraction (this `if (cands)`
         * branch), so failing probes (pre-henkan QueryRange E_INVALIDARG) don't
         * short-circuit before the one that works. Skips the Wine reconversion
         * teardown fault. */
        if (env_flag_enabled(L"AT_RECONV_EXIT")) {
            log_str("RECONV done; clean exit before edit-session teardown\r\n");
            ExitProcess(0);
        }
    }
reconv_cleanup:
    if (newRange && newRange != range) newRange->lpVtbl->Release(newRange);
    if (range) range->lpVtbl->Release(range);
    recon->lpVtbl->Release(recon);
    prov->lpVtbl->Release(prov);
    at_daemon_ms("recon_ret");
}

/* Minimal ITfEditSession wrapping do_reconversion so it runs inside a real
 * document lock. */
static ITfContext *g_reconv_ctx;
static const char *g_reconv_tag;

static HRESULT WINAPI reconv_es_QI(ITfEditSession *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_LOCAL_ITfEditSession)) {
        *ppv = iface; iface->lpVtbl->AddRef(iface); return S_OK;
    }
    *ppv = 0; return E_NOINTERFACE;
}
static ULONG WINAPI reconv_es_AddRef(ITfEditSession *iface) { (void)iface; return 2; }
static ULONG WINAPI reconv_es_Release(ITfEditSession *iface) { (void)iface; return 1; }
static HRESULT WINAPI reconv_es_DoEditSession(ITfEditSession *iface, TfEditCookie ec)
{
    (void)iface;
    do_reconversion(g_reconv_ctx, ec, g_reconv_tag);
    return S_OK;
}
static const ITfEditSessionVtbl g_reconv_es_vtbl = {
    reconv_es_QI, reconv_es_AddRef, reconv_es_Release, reconv_es_DoEditSession
};
static ITfEditSession g_reconv_es = { &g_reconv_es_vtbl };

/* --- Survive the wine-builtin msctf edit-session-teardown access violation ---
 *
 * RE finding (2026-06-24): on a SYNC|READWRITE reconversion edit session, wine's
 * builtin msctf completes the session AFTER our DoEditSession returns by releasing
 * a cached interface at <ctx-internal>+0x30 with NO null check
 * (msctf+0xF8A0: `mov (%eax),%edx` with eax = obj->field30 == NULL). On bare metal
 * that field is NULL, so the teardown faults; the container's slower timing leaves
 * it populated, hiding the bug. Our candidate list is already captured AND emitted
 * before completion runs, so we wrap the RequestEditSession call in a vectored
 * handler that catches the access violation and longjmps back (via a pre-captured
 * CONTEXT) — leaving the process warm for the next convert. Without this, warm
 * reuse is impossible and every convert pays a fresh ~1.5s TIP activation.
 *
 * Gated by AT_RECONV_KEEPALIVE (the warm-reuse mode); the one-shot path still
 * ExitProcess()es before teardown and never installs the handler. */
static PVOID g_teardown_veh;
static volatile LONG g_teardown_guard;    /* 1 only while inside the guarded call */
static volatile LONG g_teardown_tripped;  /* set by the handler after recovery */
static CONTEXT g_teardown_ctx;            /* recovery point (RtlCaptureContext) */

static LONG CALLBACK teardown_filter(EXCEPTION_POINTERS *ep)
{
    if (g_teardown_guard &&
        ep->ExceptionRecord->ExceptionCode == (DWORD)EXCEPTION_ACCESS_VIOLATION) {
        g_teardown_guard = 0;
        g_teardown_tripped = 1;
        *ep->ContextRecord = g_teardown_ctx;   /* resume right after the capture */
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void probe_reconversion(ITfContext *context, const char *tag, BOOL force)
{
    HRESULT hr = E_FAIL, hrSession = E_FAIL;
    char buf[128];
    BOOL guarded;

    /* `force` lets the resident runtime's `reconv` ctl command run regardless of
     * the AT_PROBE_RECONV env gate the one-shot drive path uses. */
    if (!force && !env_flag_enabled(L"AT_PROBE_RECONV")) return;
    if (!g_tm || !context) return;

    g_reconv_ctx = context;
    g_reconv_tag = tag;
    /* AT_NO_GUARD disables the teardown handler so we can confirm it is what
     * keeps warm reuse alive (vs. an incidental memory-layout change). */
    guarded = env_flag_enabled(L"AT_RECONV_KEEPALIVE") &&
              !env_flag_enabled(L"AT_NO_GUARD");

    if (guarded) {
        if (!g_teardown_veh)
            g_teardown_veh = AddVectoredExceptionHandler(1, teardown_filter);
        g_teardown_tripped = 0;
        /* Recovery point: if the edit-session teardown faults, the handler copies
         * this CONTEXT back and execution resumes here with g_teardown_tripped=1. */
        RtlCaptureContext(&g_teardown_ctx);
        if (!g_teardown_tripped) {
            g_teardown_guard = 1;
            /* TF_ES_SYNC(0x1) | TF_ES_READWRITE(0x6) = 0x7 */
            hr = context->lpVtbl->RequestEditSession(context, g_client_id, &g_reconv_es,
                                                     TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
            g_teardown_guard = 0;
        } else {
            log_str("RECONV: recovered from edit-session teardown AV (guarded)\r\n");
            hr = S_OK;
        }
    } else {
        /* TF_ES_SYNC(0x1) | TF_ES_READWRITE(0x6) = 0x7 */
        hr = context->lpVtbl->RequestEditSession(context, g_client_id, &g_reconv_es,
                                                 TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    }
    wsprintfA(buf, "RECONV RequestEditSession hr=0x%08lX hrSession=0x%08lX\r\n",
              (unsigned long)hr, (unsigned long)hrSession);
    log_str(buf);
    at_daemon_ms("session_done");
}
