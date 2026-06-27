/* Minimal ITextStoreACP + ITfContextOwner + composition sink for ATOK TSF driving */
#include "textstore.h"
#include <stddef.h>
#include <stdio.h>

#define TS_BUF_CHARS 4096
#ifndef TF_TF_IGNOREEND
#define TF_TF_IGNOREEND 2
#endif
#ifndef TS_IAS_QUERYONLY
#define TS_IAS_QUERYONLY 1
#endif
#ifndef TS_DEFAULT_SELECTION
#define TS_DEFAULT_SELECTION ((ULONG)-1)
#endif

typedef struct TextStore {
    ITextStoreACP ts;
    ITfContextOwner owner;
    LONG refs;
    WCHAR buf[TS_BUF_CHARS];
    LONG len;
    LONG sel_start;
    LONG sel_end;
    ITextStoreACPSink *sink;
    DWORD lock_flags;
    ULONG comp_count;
    WCHAR last_comp[256];
} TextStore;

static TextStore *g_store;
static TfEditCookie g_edit_cookie;
static ITfContext *g_comp_ctx;
static DWORD g_comp_cookie;
static ULONG g_comp_events;
static HWND g_host_wnd;
static ITfContextOwnerCompositionSink g_comp_sink;
static DWORD g_store_static_flags = TS_SS_TRANSITORY;
static BOOL g_store_interim_selection;

/* AT_QUIET: the text store is called back many times per convert; its stdout
 * logging lands on the daemon protocol pipe and adds syscalls to the hot path.
 * Suppress it in quiet mode (checked once). */
static int g_ts_quiet = -1;
static void log_str(const char *s)
{
    DWORD w;
    HANDLE h;
    if (g_ts_quiet < 0) {
        WCHAR b[8];
        DWORD n = GetEnvironmentVariableW(L"AT_QUIET", b, 8);
        g_ts_quiet = (n > 0 && b[0] && b[0] != L'0') ? 1 : 0;
    }
    if (g_ts_quiet) return;
    h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h) WriteFile(h, s, lstrlenA(s), &w, 0);
}

static void log_wstr(const char *tag, const WCHAR *ws)
{
    char line[512];
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, line, sizeof(line) - 1, 0, 0);
    if (n <= 0) {
        log_str(tag);
        log_str(" (utf8 fail)\r\n");
        return;
    }
    log_str(tag);
    log_str(line);
    log_str("\r\n");
}

/* --- IUnknown (text store) --- */
static HRESULT STDMETHODCALLTYPE ts_QI(ITextStoreACP *t, REFIID riid, void **ppv)
{
    TextStore *s = (TextStore *)t;
    if (IsEqualIID(riid, &IID_IUnknown)) {
        *ppv = t;
        t->lpVtbl->AddRef(t);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_ITextStoreACP)) {
        *ppv = t;
        t->lpVtbl->AddRef(t);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_ITfContextOwner)) {
        *ppv = &s->owner;
        s->owner.lpVtbl->AddRef(&s->owner);
        return S_OK;
    }
    if (IsEqualIID(riid, &IID_ITfContextOwnerCompositionSink)) {
        *ppv = &g_comp_sink;
        g_comp_sink.lpVtbl->AddRef(&g_comp_sink);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ts_AddRef(ITextStoreACP *t)
{
    TextStore *s = (TextStore *)t;
    return (ULONG)InterlockedIncrement(&s->refs);
}

static ULONG STDMETHODCALLTYPE ts_Release(ITextStoreACP *t)
{
    TextStore *s = (TextStore *)t;
    ULONG r = (ULONG)InterlockedDecrement(&s->refs);
    if (!r) {
        if (g_store == s) g_store = 0;
        HeapFree(GetProcessHeap(), 0, s);
    }
    return r;
}

static HRESULT STDMETHODCALLTYPE ts_AdviseSink(ITextStoreACP *t, REFIID riid, IUnknown *punk, DWORD mask)
{
    TextStore *s = (TextStore *)t;
    (void)mask;
    if (!IsEqualIID(riid, &IID_ITextStoreACPSink)) return E_INVALIDARG;
    if (s->sink) s->sink->lpVtbl->Release(s->sink);
    s->sink = 0;
    if (punk)
        punk->lpVtbl->QueryInterface(punk, &IID_ITextStoreACPSink, (void **)&s->sink);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_UnadviseSink(ITextStoreACP *t, IUnknown *punk)
{
    TextStore *s = (TextStore *)t;
    (void)punk;
    if (s->sink) s->sink->lpVtbl->Release(s->sink);
    s->sink = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_RequestLock(ITextStoreACP *t, DWORD dwLockFlags, HRESULT *phrSession)
{
    TextStore *s = (TextStore *)t;
    char buf[128];
    s->lock_flags = dwLockFlags;
    if (phrSession) *phrSession = S_OK;
    wsprintfA(buf, "STORE RequestLock flags=0x%08lX sink=%p\r\n",
              (unsigned long)dwLockFlags, s->sink);
    log_str(buf);
    if (s->sink)
        s->sink->lpVtbl->OnLockGranted(s->sink, dwLockFlags);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_GetStatus(ITextStoreACP *t, TS_STATUS *pdcs)
{
    if (!pdcs) return E_INVALIDARG;
    pdcs->dwDynamicFlags = 0;
    pdcs->dwStaticFlags = g_store_static_flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_QueryInsert(ITextStoreACP *t, LONG s0, LONG s1, ULONG cch, LONG *rs, LONG *re)
{
    TextStore *st = (TextStore *)t;
    LONG old_span;
    char buf[160];
    if (s0 < 0) s0 = 0;
    if (s1 < s0) s1 = s0;
    if (s0 > st->len) s0 = st->len;
    if (s1 > st->len) s1 = st->len;
    old_span = s1 - s0;
    if (rs) *rs = s0;
    if (re) *re = s0 + (LONG)cch;
    wsprintfA(buf, "STORE QueryInsert start=%ld end=%ld cch=%lu len=%ld result=%ld..%ld\r\n",
              s0, s1, (unsigned long)cch, st->len,
              rs ? *rs : -1, re ? *re : -1);
    log_str(buf);
    if (st->len - old_span + (LONG)cch > TS_BUF_CHARS) return TS_E_INVALIDPOS;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_GetSelection(ITextStoreACP *t, ULONG idx, ULONG count, TS_SELECTION_ACP *sel, ULONG *fetched)
{
    TextStore *s = (TextStore *)t;
    char buf[160];
    if ((idx != 0 && idx != TS_DEFAULT_SELECTION) || count < 1) {
        if (fetched) *fetched = 0;
        wsprintfA(buf, "STORE GetSelection idx=%lu count=%lu -> fetched=0\r\n",
                  (unsigned long)idx, (unsigned long)count);
        log_str(buf);
        return S_OK;
    }
    sel[0].acpStart = s->sel_start;
    sel[0].acpEnd = s->sel_end;
    sel[0].style.ase = g_store_interim_selection ? TS_AE_START : TS_AE_END;
    sel[0].style.fInterimChar = g_store_interim_selection;
    if (fetched) *fetched = 1;
    wsprintfA(buf, "STORE GetSelection idx=%lu count=%lu len=%ld sel=%ld..%ld interim=%d\r\n",
              (unsigned long)idx, (unsigned long)count, s->len,
              s->sel_start, s->sel_end, g_store_interim_selection ? 1 : 0);
    log_str(buf);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_SetSelection(ITextStoreACP *t, ULONG count, const TS_SELECTION_ACP *sel)
{
    TextStore *s = (TextStore *)t;
    char buf[160];
    if (count < 1) return E_INVALIDARG;
    s->sel_start = sel[0].acpStart;
    s->sel_end = sel[0].acpEnd;
    if (s->sel_start < 0) s->sel_start = 0;
    if (s->sel_end < s->sel_start) s->sel_end = s->sel_start;
    if (s->sel_start > s->len) s->sel_start = s->len;
    if (s->sel_end > s->len) s->sel_end = s->len;
    wsprintfA(buf, "STORE SetSelection count=%lu sel=%ld..%ld len=%ld\r\n",
              (unsigned long)count, s->sel_start, s->sel_end, s->len);
    log_str(buf);
    if (s->sink) s->sink->lpVtbl->OnSelectionChange(s->sink);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_GetText(ITextStoreACP *t, LONG acpStart, LONG acpEnd, WCHAR *pch, ULONG cchReq,
    ULONG *pcchRet, TS_RUNINFO *ri, ULONG cri, ULONG *pri, LONG *next)
{
    TextStore *s = (TextStore *)t;
    LONG n, i;
    LONG next_acp;
    char buf[192];
    if (acpEnd < 0) acpEnd = s->len;
    if (acpStart < 0) acpStart = 0;
    if (acpEnd > s->len) acpEnd = s->len;
    n = acpEnd - acpStart;
    if (n < 0) n = 0;
    if ((ULONG)n > cchReq) n = (LONG)cchReq;
    for (i = 0; pch && i < n; i++)
        pch[i] = s->buf[acpStart + i];
    if (pcchRet) *pcchRet = (ULONG)n;
    if (ri && cri > 0) {
        ri[0].uCount = (ULONG)n;
        ri[0].type = TS_RT_PLAIN;
        if (pri) *pri = 1;
    } else if (pri) {
        *pri = 0;
    }
    next_acp = acpStart + n;
    if (next) *next = next_acp;
    wsprintfA(buf, "STORE GetText start=%ld end=%ld req=%lu len=%ld got=%ld next=%ld runs=%lu\r\n",
              acpStart, acpEnd, (unsigned long)cchReq, s->len, n, next_acp,
              pri ? (unsigned long)*pri : 0);
    log_str(buf);
    return S_OK;
}

static void notify_text_change(TextStore *s, LONG start, LONG oldEnd, LONG newEnd)
{
    TS_TEXTCHANGE ch;
    if (!s->sink) return;
    ch.acpStart = start;
    ch.acpOldEnd = oldEnd;
    ch.acpNewEnd = newEnd;
    s->sink->lpVtbl->OnTextChange(s->sink, TS_AS_TEXT_CHANGE, &ch);
}

static HRESULT STDMETHODCALLTYPE ts_SetText(ITextStoreACP *t, DWORD flags, LONG start, LONG end, const WCHAR *pch, ULONG cch, TS_TEXTCHANGE *ch)
{
    TextStore *s = (TextStore *)t;
    LONG oldLen = s->len;
    LONG delta;
    LONG i;
    char buf[192];
    wsprintfA(buf, "STORE SetText flags=0x%08lX start=%ld end=%ld cch=%lu len=%ld\r\n",
              (unsigned long)flags, start, end, (unsigned long)cch, s->len);
    log_str(buf);
    if (pch && cch > 0) log_wstr("  ", pch);
    if (end < 0) end = s->len;
    if (start < 0) start = 0;
    if (end > s->len) end = s->len;
    if (end < start) end = start;
    delta = (LONG)cch - (end - start);
    if (s->len + delta > TS_BUF_CHARS) return TS_E_INVALIDPOS;
    MoveMemory(s->buf + start + (LONG)cch, s->buf + end, (s->len - end) * sizeof(WCHAR));
    for (i = 0; i < (LONG)cch; i++)
        s->buf[start + i] = pch[i];
    s->len += delta;
    s->sel_start = start + (LONG)cch;
    s->sel_end = s->sel_start;
    if (ch) {
        ch->acpStart = start;
        ch->acpOldEnd = end;
        ch->acpNewEnd = start + (LONG)cch;
    }
    notify_text_change(s, start, end, start + (LONG)cch);
    (void)oldLen;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_InsertTextAtSelection(ITextStoreACP *t, DWORD flags, const WCHAR *pch, ULONG cch, LONG *ps, LONG *pe, TS_TEXTCHANGE *ch)
{
    TextStore *s = (TextStore *)t;
    LONG start = s->sel_start;
    LONG end = s->sel_end;
    char buf[192];
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (start > s->len) start = s->len;
    if (end > s->len) end = s->len;
    if (ps) *ps = start;
    if (pe) *pe = start + (LONG)cch;
    wsprintfA(buf, "STORE InsertTextAtSelection flags=0x%08lX cch=%lu sel=%ld..%ld query=%d\r\n",
              (unsigned long)flags, (unsigned long)cch, start, end,
              (flags & TS_IAS_QUERYONLY) ? 1 : 0);
    log_str(buf);
    if (flags & TS_IAS_QUERYONLY) {
        if (ch) {
            ch->acpStart = start;
            ch->acpOldEnd = end;
            ch->acpNewEnd = start + (LONG)cch;
        }
        return S_OK;
    }
    return ts_SetText(t, flags, start, end, pch, cch, ch);
}

static HRESULT STDMETHODCALLTYPE ts_GetEndACP(ITextStoreACP *t, LONG *pacp)
{
    TextStore *s = (TextStore *)t;
    if (pacp) *pacp = s->len;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ts_notimpl(ITextStoreACP *t)
{
    (void)t;
    return E_NOTIMPL;
}

/* --- ITfContextOwner (same object as text store) --- */
static HRESULT STDMETHODCALLTYPE co_QI(ITfContextOwner *o, REFIID riid, void **ppv)
{
    TextStore *s = (TextStore *)((BYTE *)o - offsetof(TextStore, owner));
    return ts_QI(&s->ts, riid, ppv);
}

static ULONG STDMETHODCALLTYPE co_AddRef(ITfContextOwner *o)
{
    TextStore *s = (TextStore *)((BYTE *)o - offsetof(TextStore, owner));
    return ts_AddRef(&s->ts);
}

static ULONG STDMETHODCALLTYPE co_Release(ITfContextOwner *o)
{
    TextStore *s = (TextStore *)((BYTE *)o - offsetof(TextStore, owner));
    return ts_Release(&s->ts);
}

static HRESULT STDMETHODCALLTYPE co_GetACPFromPoint(ITfContextOwner *o, const POINT *pt, DWORD flags, LONG *pacp)
{
    (void)o;
    (void)pt;
    (void)flags;
    if (pacp) *pacp = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_GetTextExt(ITfContextOwner *o, TfEditCookie ec, LONG start, LONG end, RECT *rc, BOOL *clipped)
{
    (void)o;
    (void)ec;
    (void)start;
    (void)end;
    if (rc) SetRectEmpty(rc);
    if (clipped) *clipped = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_GetScreenExt(ITfContextOwner *o, TfEditCookie ec, LONG start, LONG end, RECT *rc)
{
    (void)o;
    (void)ec;
    (void)start;
    (void)end;
    if (rc) SetRectEmpty(rc);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_GetStatus(ITfContextOwner *o, TS_STATUS *st)
{
    (void)o;
    if (!st) return E_INVALIDARG;
    st->dwDynamicFlags = 0;
    st->dwStaticFlags = g_store_static_flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE co_GetWnd(ITfContextOwner *o, HWND *hwnd)
{
    (void)o;
    if (!hwnd) return E_INVALIDARG;
    *hwnd = g_host_wnd;
    return g_host_wnd ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE co_QueryInsert(ITfContextOwner *o, LONG start, LONG end, ULONG cch, LONG *rs, LONG *re)
{
    TextStore *s = (TextStore *)((BYTE *)o - offsetof(TextStore, owner));
    return ts_QueryInsert(&s->ts, start, end, cch, rs, re);
}

static ITfContextOwnerVtbl g_co_vtbl = {
    (void *)co_QI, (void *)co_AddRef, (void *)co_Release,
    (void *)co_GetACPFromPoint, (void *)co_GetTextExt, (void *)co_GetScreenExt,
    (void *)co_GetStatus, (void *)co_GetWnd, (void *)co_QueryInsert
};

/* Slot order MUST match ITextStoreACPVtbl in <textstor.h>. A previous version
 * miscounted the no-impl filler and landed ts_InsertTextAtSelection on the
 * InsertEmbeddedAtSelection slot (14->15) and ts_GetEndACP on the
 * GetActiveView slot (21->22). That made the TIP's InsertTextAtSelection probe
 * (forwarded by MsctfShim) hit ts_notimpl -> E_NOTIMPL, so no converted text
 * ever reached the store. Each slot is now annotated to keep it aligned. */
static ITextStoreACPVtbl g_ts_vtbl = {
    (void *)ts_QI, (void *)ts_AddRef, (void *)ts_Release,
    (void *)ts_AdviseSink,             /* 1  AdviseSink */
    (void *)ts_UnadviseSink,           /* 2  UnadviseSink */
    (void *)ts_RequestLock,            /* 3  RequestLock */
    (void *)ts_GetStatus,              /* 4  GetStatus */
    (void *)ts_QueryInsert,            /* 5  QueryInsert */
    (void *)ts_GetSelection,           /* 6  GetSelection */
    (void *)ts_SetSelection,           /* 7  SetSelection */
    (void *)ts_GetText,                /* 8  GetText */
    (void *)ts_SetText,                /* 9  SetText */
    (void *)ts_notimpl,                /* 10 GetFormattedText */
    (void *)ts_notimpl,                /* 11 GetEmbedded */
    (void *)ts_notimpl,                /* 12 QueryInsertEmbedded */
    (void *)ts_notimpl,                /* 13 InsertEmbedded */
    (void *)ts_InsertTextAtSelection,  /* 14 InsertTextAtSelection */
    (void *)ts_notimpl,                /* 15 InsertEmbeddedAtSelection */
    (void *)ts_notimpl,                /* 16 RequestSupportedAttrs */
    (void *)ts_notimpl,                /* 17 RequestAttrsAtPosition */
    (void *)ts_notimpl,                /* 18 RequestAttrsTransitioningAtPosition */
    (void *)ts_notimpl,                /* 19 FindNextAttrTransition */
    (void *)ts_notimpl,                /* 20 RetrieveRequestedAttrs */
    (void *)ts_GetEndACP,              /* 21 GetEndACP */
    (void *)ts_notimpl,                /* 22 GetActiveView */
    (void *)ts_notimpl,                /* 23 GetACPFromPoint */
    (void *)ts_notimpl,                /* 24 GetTextExt */
    (void *)ts_notimpl,                /* 25 GetScreenExt */
    (void *)ts_notimpl                 /* 26 GetWnd */
};

/* --- composition sink --- */
static HRESULT STDMETHODCALLTYPE cs_QI(ITfContextOwnerCompositionSink *s, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfContextOwnerCompositionSink)) {
        *ppv = s;
        s->lpVtbl->AddRef(s);
        return S_OK;
    }
    *ppv = 0;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE cs_AddRef(ITfContextOwnerCompositionSink *s) { (void)s; return 2; }
static ULONG STDMETHODCALLTYPE cs_Release(ITfContextOwnerCompositionSink *s) { (void)s; return 1; }

static HRESULT STDMETHODCALLTYPE cs_OnStartComposition(ITfContextOwnerCompositionSink *s, ITfComposition *c, BOOL *ok)
{
    (void)s;
    (void)c;
    if (ok) *ok = TRUE;
    g_comp_events++;
    log_str("COMP: OnStartComposition\r\n");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cs_OnUpdateComposition(ITfContextOwnerCompositionSink *s, ITfComposition *c, ITfRange *range)
{
    WCHAR tmp[256];
    ULONG got = 0;
    char buf[192];
    (void)s;
    (void)c;
    g_comp_events++;
    if (range && g_edit_cookie) {
        HRESULT hr = range->lpVtbl->GetText(range, g_edit_cookie, 0, tmp, 255, &got);
        wsprintfA(buf, "COMP: OnUpdateComposition range=%p ec=%lu hr=0x%08lX got=%lu\r\n",
                  range, (unsigned long)g_edit_cookie, (unsigned long)hr, (unsigned long)got);
        log_str(buf);
        if (hr == S_OK && got > 0) {
            tmp[got] = 0;
            if (g_store) {
                lstrcpynW(g_store->last_comp, tmp, 255);
                g_store->comp_count++;
            }
            log_wstr("COMP: OnUpdateComposition ", tmp);
        } else if (hr == S_OK) {
            log_str("COMP: OnUpdateComposition (empty text)\r\n");
        } else {
            log_str("COMP: OnUpdateComposition (GetText failed)\r\n");
        }
    } else {
        wsprintfA(buf, "COMP: OnUpdateComposition range=%p ec=%lu\r\n",
                  range, (unsigned long)g_edit_cookie);
        log_str(buf);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cs_OnEndComposition(ITfContextOwnerCompositionSink *s, ITfComposition *c)
{
    (void)s;
    (void)c;
    g_comp_events++;
    log_str("COMP: OnEndComposition\r\n");
    return S_OK;
}

static ITfContextOwnerCompositionSinkVtbl g_cs_vtbl = {
    (void *)cs_QI, (void *)cs_AddRef, (void *)cs_Release,
    (void *)cs_OnStartComposition, (void *)cs_OnUpdateComposition, (void *)cs_OnEndComposition
};

static ITfContextOwnerCompositionSink g_comp_sink = { &g_cs_vtbl };

IUnknown *TextStore_Create(void)
{
    TextStore *s = (TextStore *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TextStore));
    if (!s) return 0;
    s->ts.lpVtbl = &g_ts_vtbl;
    s->owner.lpVtbl = &g_co_vtbl;
    s->refs = 1;
    s->sel_end = 0;
    g_store = s;
    return (IUnknown *)&s->ts;
}

void TextStore_SetHostWindow(HWND hwnd) { g_host_wnd = hwnd; }

void TextStore_SetEditCookie(TfEditCookie cookie) { g_edit_cookie = cookie; }

void TextStore_SetStaticFlags(DWORD flags) { g_store_static_flags = flags; }

void TextStore_SetInterimSelection(BOOL on) { g_store_interim_selection = on; }

void TextStore_LogContent(void)
{
    if (!g_store) return;
    g_store->buf[g_store->len] = 0;
    log_wstr("STORE text: ", g_store->buf);
}

/* Copy the current committed store text (UTF-16) into out; returns the length.
 * Used by the fast `henkan` path to read back the committed top-1 candidate. */
int TextStore_GetText(WCHAR *out, int max)
{
    int n;
    if (!g_store || !out || max <= 0) { if (out && max > 0) out[0] = 0; return 0; }
    n = (int)g_store->len;
    if (n > max - 1) n = max - 1;
    {
        int i;
        for (i = 0; i < n; i++) out[i] = g_store->buf[i];
    }
    out[n] = 0;
    return n;
}

void TextStore_AppendChar(WCHAR ch)
{
    TextStore *s = g_store;
    TS_TEXTCHANGE chg;

    if (!s || !ch) return;
    if (s->len >= TS_BUF_CHARS - 1) return;

    s->buf[s->len++] = ch;
    s->buf[s->len] = 0;
    if (g_store_interim_selection) {
        s->sel_start = 0;
        s->sel_end = s->len;
    } else {
        s->sel_start = s->len;
        s->sel_end = s->len;
    }

    chg.acpStart = s->len - 1;
    chg.acpOldEnd = s->len - 1;
    chg.acpNewEnd = s->len;
    notify_text_change(s, chg.acpStart, chg.acpOldEnd, chg.acpNewEnd);
    if (s->sink) s->sink->lpVtbl->OnSelectionChange(s->sink);
}

void TextStore_LogSelection(ITfContext *context, const char *tag)
{
    TF_SELECTION sel;
    ITfRange *range = 0;
    WCHAR tmp[256];
    ULONG fetched = 0;
    ULONG got = 0;
    HRESULT hr;
    char buf[160];

    if (!context || !g_edit_cookie) {
        log_str("SELECTION: unavailable\r\n");
        return;
    }

    ZeroMemory(&sel, sizeof(sel));
    hr = context->lpVtbl->GetSelection(context, g_edit_cookie, 0, 1, &sel, &fetched);
    wsprintfA(buf, "SELECTION [%s] GetSelection hr=0x%08lX fetched=%lu\r\n",
              tag ? tag : "", (unsigned long)hr, (unsigned long)fetched);
    log_str(buf);
    if (hr != S_OK || fetched < 1) return;

    wsprintfA(buf, "SELECTION [%s] interim=%d style=%lu\r\n",
              tag ? tag : "", sel.style.fInterimChar ? 1 : 0, (unsigned long)sel.style.ase);
    log_str(buf);
    range = sel.range;
    wsprintfA(buf, "SELECTION [%s] range=%p\r\n", tag ? tag : "", range);
    log_str(buf);
    if (!range) {
        log_str("SELECTION: no range\r\n");
        return;
    }

    got = 0;
    hr = range->lpVtbl->GetText(range, g_edit_cookie, TF_TF_IGNOREEND, tmp, 255, &got);
    wsprintfA(buf, "SELECTION [%s] GetText hr=0x%08lX got=%lu\r\n",
              tag ? tag : "", (unsigned long)hr, (unsigned long)got);
    log_str(buf);
    if (hr == S_OK && got > 0) {
        tmp[got] = 0;
        log_wstr("SELECTION text: ", tmp);
    }
    range->lpVtbl->Release(range);
}

ULONG TextStore_GetCompositionCount(void) { return g_comp_events; }

HRESULT CompositionSink_Advise(ITfContext *context)
{
    ITfSource *src = 0;
    HRESULT hr;
    if (!context) return E_POINTER;
    g_comp_ctx = context;
    hr = context->lpVtbl->QueryInterface(context, &IID_ITfSource, (void **)&src);
    if (hr != S_OK || !src) return hr;
    hr = src->lpVtbl->AdviseSink(src, &IID_ITfContextOwnerCompositionSink, (IUnknown *)&g_comp_sink, &g_comp_cookie);
    src->lpVtbl->Release(src);
    return hr;
}

void CompositionSink_Unadvise(ITfContext *context)
{
    ITfSource *src = 0;
    if (!context || !g_comp_cookie) return;
    if (context->lpVtbl->QueryInterface(context, &IID_ITfSource, (void **)&src) == S_OK && src) {
        src->lpVtbl->UnadviseSink(src, g_comp_cookie);
        src->lpVtbl->Release(src);
    }
    g_comp_cookie = 0;
}

void Composition_PollContext(ITfContext *context, const char *tag)
{
    ITfContextComposition *cc = 0;
    IEnumITfCompositionView *enu = 0;
    ITfCompositionView *view = 0;
    ITfRange *range = 0;
    WCHAR tmp[512];
    ULONG got = 0;
    ULONG n = 0;
    HRESULT hr;
    char buf[96];

    if (!context || !g_edit_cookie) return;
    hr = context->lpVtbl->QueryInterface(context, &IID_ITfContextComposition, (void **)&cc);
    if (hr != S_OK || !cc) {
        log_str("COMP poll: no ITfContextComposition\r\n");
        return;
    }
    hr = cc->lpVtbl->EnumCompositions(cc, &enu);
    if (hr != S_OK || !enu) {
        wsprintfA(buf, "COMP poll [%s]: EnumCompositions hr=0x%08lX\r\n", tag, (unsigned long)hr);
        log_str(buf);
        cc->lpVtbl->Release(cc);
        return;
    }
    wsprintfA(buf, "COMP poll [%s]:\r\n", tag);
    log_str(buf);
    while (enu->lpVtbl->Next(enu, 1, &view, 0) == S_OK && view) {
        n++;
        hr = view->lpVtbl->GetRange(view, &range);
        if (hr == S_OK && range) {
            got = 0;
            hr = range->lpVtbl->GetText(range, g_edit_cookie, TF_TF_IGNOREEND, tmp, 511, &got);
            if (hr == S_OK && got > 0) {
                tmp[got] = 0;
                if (g_store) {
                    lstrcpynW(g_store->last_comp, tmp, 255);
                    g_store->comp_count++;
                }
                log_wstr("  view range: ", tmp);
            } else {
                wsprintfA(buf, "  view #%lu GetText hr=0x%08lX got=%lu\r\n",
                          (unsigned long)n, (unsigned long)hr, (unsigned long)got);
                log_str(buf);
            }
            range->lpVtbl->Release(range);
            range = 0;
        } else {
            wsprintfA(buf, "  view #%lu GetRange hr=0x%08lX\r\n", (unsigned long)n, (unsigned long)hr);
            log_str(buf);
        }
        view->lpVtbl->Release(view);
        view = 0;
    }
    if (!n)
        log_str("  (no active compositions)\r\n");
    enu->lpVtbl->Release(enu);
    cc->lpVtbl->Release(cc);
}
