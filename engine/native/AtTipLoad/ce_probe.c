/*
 * ATOK conversion-engine (CE) shared-memory + IMM candidate probes.
 *
 * Extracted from AtTipLoad.c. Read-only sniffing of ATOK's CE shared mapping
 * (the +0x72FE/+0x737E/+0x74CE counters that move during composition/candidate/
 * commit), the registry-derived CE name suffixes, UTF-16 string scans of the CE
 * and sibling client mappings, and the IMM candidate list. These are the
 * "watch ATOK-owned backend state" diagnostics; shared globals/helpers come
 * from AtTipLoad.c via at_runtime.h, while the CE name table below is
 * private to this module.
 */
#include <windows.h>
#include <objbase.h>
#include <msctf.h>
#include <imm.h>
#include "at_runtime.h"

static WCHAR upper_ascii_w(WCHAR ch)
{
    if (ch >= L'a' && ch <= L'z') return (WCHAR)(ch - (L'a' - L'A'));
    return ch;
}

static int streqi_w(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (upper_ascii_w(*a) != upper_ascii_w(*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* CE shared-mapping name candidates, resolved from registry/env/user. The TIP
 * names objects ..._dev (Wine) vs ..._<user> (host); we try each suffix. */
static wchar_t g_ce_name[128];
#define MAX_CE_NAMES 8
static wchar_t g_ce_names[MAX_CE_NAMES][128];
static unsigned g_ce_name_count;
static unsigned g_ce_active_index;

void load_registry_names(wchar_t *machine, int mlen, wchar_t *ver, int vlen)
{
    HKEY hk;
    DWORD n;
    wchar_t env_name[128];
    wchar_t user[64];
    DWORD user_len;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Justsystem\\Common\\UserName", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        n = (DWORD)mlen;
        RegQueryValueExW(hk, NULL, 0, 0, (LPBYTE)machine, &n);
        RegCloseKey(hk);
    } else {
        lstrcpynW(machine, L"USER", mlen);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Justsystem\\ATOK\\31.0", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        n = (DWORD)vlen;
        RegQueryValueExW(hk, L"Version", 0, 0, (LPBYTE)ver, &n);
        RegCloseKey(hk);
    } else {
        lstrcpynW(ver, L"31.2", vlen);
    }

    g_ce_name_count = 0;
    g_ce_active_index = 0;

    if (GetEnvironmentVariableW(L"AT_CE_NAME", env_name, (DWORD)(sizeof(env_name) / sizeof(env_name[0]))) > 0) {
        lstrcpynW(g_ce_names[g_ce_name_count++], env_name, 128);
    }

#define ADD_CE_SUFFIX(suffix_) do { \
        wchar_t _name[128]; \
        unsigned _i; \
        if ((suffix_) && (suffix_)[0] && g_ce_name_count < MAX_CE_NAMES) { \
            wsprintfW(_name, L"AtokPrivateNamespace\\JsMmf_ATOK31CE_SHARED_DATA%s", (suffix_)); \
            for (_i = 0; _i < g_ce_name_count; _i++) { \
                if (streqi_w(g_ce_names[_i], _name)) break; \
            } \
            if (_i == g_ce_name_count) lstrcpynW(g_ce_names[g_ce_name_count++], _name, 128); \
        } \
    } while (0)

    ADD_CE_SUFFIX(machine);
    user_len = (DWORD)(sizeof(user) / sizeof(user[0]));
    if (GetUserNameW(user, &user_len)) ADD_CE_SUFFIX(user);
    ADD_CE_SUFFIX(L"dev");
    ADD_CE_SUFFIX(L"USER");
    ADD_CE_SUFFIX(L"ASUS");

#undef ADD_CE_SUFFIX

    if (g_ce_name_count == 0) {
        lstrcpynW(g_ce_names[g_ce_name_count++], L"AtokPrivateNamespace\\JsMmf_ATOK31CE_SHARED_DATAUSER", 128);
    }
    lstrcpynW(g_ce_name, g_ce_names[0], 128);
}

static void ce_name_to_ascii(const WCHAR *name, char *buf, DWORD buflen)
{
    DWORD i;
    if (!buf || !buflen) return;
    if (!name) {
        const char *none = "(none)";
        DWORD j;
        for (j = 0; j + 1 < buflen && none[j]; j++) buf[j] = none[j];
        buf[j] = 0;
        return;
    }
    for (i = 0; i + 1 < buflen && name[i]; i++) {
        WCHAR ch = name[i];
        buf[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
    }
    buf[i] = 0;
}

static BOOL read_ce_snapshot_words(WORD *w72fe, WORD *w737e, WORD *w74ce, const WCHAR **used_name)
{
    HANDLE hf = NULL;
    void *view;
    unsigned pass;

    if (w72fe) *w72fe = 0;
    if (w737e) *w737e = 0;
    if (w74ce) *w74ce = 0;
    if (used_name) *used_name = NULL;

    for (pass = 0; pass < g_ce_name_count + 1; pass++) {
        unsigned i;
        if (!g_ce_name_count) break;
        i = (pass == 0) ? g_ce_active_index : (pass - 1);
        if (i >= g_ce_name_count || (pass != 0 && i == g_ce_active_index)) continue;

        hf = OpenFileMappingW(FILE_MAP_READ, FALSE, g_ce_names[i]);
        if (!hf) continue;
        view = MapViewOfFile(hf, FILE_MAP_READ, 0, 0, 0x74D0);
        if (view) {
            if (w72fe) *w72fe = *(WORD *)((BYTE *)view + 0x72FE);
            if (w737e) *w737e = *(WORD *)((BYTE *)view + 0x737E);
            if (w74ce) *w74ce = *(WORD *)((BYTE *)view + 0x74CE);
            UnmapViewOfFile(view);
            CloseHandle(hf);
            g_ce_active_index = i;
            lstrcpynW(g_ce_name, g_ce_names[i], 128);
            if (used_name) *used_name = g_ce_names[i];
            return TRUE;
        }
        CloseHandle(hf);
    }

    return FALSE;
}

static WORD read_ce_word(unsigned off)
{
    HANDLE hf;
    void *view;
    WORD v = 0;
    const WCHAR *name = NULL;

    if (read_ce_snapshot_words(NULL, NULL, NULL, &name) && name) {
        hf = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    } else {
        hf = OpenFileMappingW(FILE_MAP_READ, FALSE, g_ce_name);
    }
    if (!hf) return 0;
    view = MapViewOfFile(hf, FILE_MAP_READ, 0, 0, off + 2);
    if (view) {
        v = *(WORD *)((BYTE *)view + off);
        UnmapViewOfFile(view);
    }
    CloseHandle(hf);
    return v;
}

void log_ce_snapshot(const char *tag)
{
    char buf[320];
    char name[160];
    WORD w72fe = 0, w737e = 0, w74ce = 0;
    const WCHAR *used_name = NULL;

    read_ce_snapshot_words(&w72fe, &w737e, &w74ce, &used_name);
    ce_name_to_ascii(used_name, name, (DWORD)(sizeof(name) / sizeof(name[0])));
    wsprintfA(buf, "%s CE name=\"%s\" CE+0x72FE=0x%04X CE+0x737E=0x%04X CE+0x74CE=0x%04X\r\n",
              tag, name, w72fe, w737e, w74ce);
    log_str(buf);
}

/* Find a wide substring (no wcsstr in this freestanding build). */
static const WCHAR *wfind(const WCHAR *hay, const WCHAR *needle)
{
    int nl = lstrlenW(needle), i, j;
    if (!nl) return hay;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j] == needle[j]; j++) {}
        if (!needle[j]) return hay + i;
    }
    return NULL;
}

/* Scan one named shared mapping for UTF-16 strings (kana / CJK / fullwidth /
 * ASCII). Logs each run with its byte offset. */
static void scan_mapping_strings(const WCHAR *name, const char *tag)
{
    HANDLE hf;
    BYTE *view;
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T region = 0, off;
    char hdr[220], name_a[180];
    unsigned found = 0;

    ce_name_to_ascii(name, name_a, (DWORD)(sizeof(name_a) / sizeof(name_a[0])));
    hf = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (!hf) {
        /* AtNsShim creates the AT objects in the GLOBAL namespace (it strips the
         * AtokPrivateNamespace\ prefix), so a reader must open the BARE name. Retry
         * without the prefix before giving up. */
        const WCHAR *prefix = L"AtokPrivateNamespace\\";
        const WCHAR *bare = name;
        const WCHAR *p = name, *q = prefix;
        while (*q && *p == *q) { p++; q++; }
        if (!*q) { bare = p; hf = OpenFileMappingW(FILE_MAP_READ, FALSE, bare); }
    }
    if (!hf) {
        wsprintfA(hdr, "=== scan [%s] \"%s\" OPEN FAILED ===\r\n", tag, name_a);
        log_str(hdr);
        return;
    }
    view = (BYTE *)MapViewOfFile(hf, FILE_MAP_READ, 0, 0, 0); /* whole mapping */
    if (!view) { CloseHandle(hf); return; }
    if (VirtualQuery(view, &mbi, sizeof(mbi))) region = mbi.RegionSize;

    wsprintfA(hdr, "=== scan [%s] \"%s\" region=0x%lX ===\r\n",
              tag, name_a, (unsigned long)region);
    log_str(hdr);

    off = 0;
    while (off + 2 <= region) {
        WCHAR *w = (WCHAR *)(view + off);
        SIZE_T run = 0;
        BOOL has_jp = FALSE;
        while (off + (run + 1) * 2 <= region) {
            WCHAR c = w[run];
            BOOL ok = (c >= 0x3040 && c <= 0x30FF) ||   /* kana */
                      (c >= 0x4E00 && c <= 0x9FFF) ||   /* CJK unified */
                      (c >= 0xFF00 && c <= 0xFFEF) ||   /* fullwidth/halfwidth */
                      (c >= 0x20 && c <= 0x7E);         /* ASCII printable */
            if (!ok) break;
            if (c >= 0x3040) has_jp = TRUE;
            run++;
        }
        if (run >= 2 && has_jp) {
            char line[512];
            int n;
            wsprintfA(line, "  +0x%05lX (%lu)= ", (unsigned long)off, (unsigned long)run);
            log_str(line);
            n = WideCharToMultiByte(CP_UTF8, 0, w, (int)run, line, (int)sizeof(line) - 1, 0, 0);
            if (n > 0) { line[n] = 0; log_str(line); }
            log_str("\r\n");
            if (++found >= 300) { log_str("  scan: cap 300\r\n"); break; }
        }
        off += (run >= 2 ? run * 2 : 2);
    }
    {
        char fb[80];
        wsprintfA(fb, "  scan: %u jp-strings\r\n", found);
        log_str(fb);
    }
    UnmapViewOfFile(view);
    CloseHandle(hf);
}

/* Scan the CE map plus its sibling ATOK client mappings for the reading
 * (にほんご) and candidate alternatives (日本語, 二本後, ...). ATOK draws its
 * own candidate window from these, so the list lives here even though it never
 * surfaces via TSF. Gated by AT_SCAN_CE. */
void scan_ce_strings(const char *tag)
{
    const WCHAR *ce = NULL;
    WCHAR suffix[40] = {0};
    WCHAR nm[160];
    const WCHAR *vers[] = { L"31.2.5", L"31.2" };
    unsigned v;

    if (!env_flag_enabled(L"AT_SCAN_CE")) return;

    /* Resolve the per-user object suffix robustly: AT_NS_SUFFIX wins, else the
     * Wine user name (the TIP suffixes objects with it — e.g. "noatdk"), else
     * whatever the CE snapshot resolved. The numbered _<suffix>_0..9 slots below
     * are where per-conversion state lands and were never scanned before. */
    if (GetEnvironmentVariableW(L"AT_NS_SUFFIX", suffix, 40) == 0) {
        DWORD ul = 40;
        if (!GetUserNameW(suffix, &ul)) suffix[0] = 0;
    }
    if (read_ce_snapshot_words(NULL, NULL, NULL, &ce) && ce) {
        if (!suffix[0]) {
            const WCHAR *p = wfind(ce, L"SHARED_DATA");
            if (p) lstrcpynW(suffix, p + lstrlenW(L"SHARED_DATA"), 40);
        }
        scan_mapping_strings(ce, tag);
    }
    if (suffix[0]) {
        wsprintfW(nm, L"AtokPrivateNamespace\\JsMmf_ATOK31CE_SHARED_DATA%s", suffix);
        scan_mapping_strings(nm, tag);
    }
    /* General client shared memory + common memory (per version, base + the
     * numbered _0..9 per-conversion slots) — the likely homes for conversion
     * state / candidate surfaces. */
    for (v = 0; v < sizeof(vers) / sizeof(vers[0]); v++) {
        int slot;
        wsprintfW(nm, L"AtokPrivateNamespace\\JsMmf_ATOK_Shared_Memory_%s_%s", vers[v], suffix);
        scan_mapping_strings(nm, tag);
        wsprintfW(nm, L"AtokPrivateNamespace\\JsMmf_ATOK_Shared_CommonMemory_%s_%s", vers[v], suffix);
        scan_mapping_strings(nm, tag);
        for (slot = 0; slot < 10; slot++) {
            wsprintfW(nm, L"AtokPrivateNamespace\\JsMmf_ATOK_Shared_Memory_%s_%s_%d", vers[v], suffix, slot);
            scan_mapping_strings(nm, tag);
            wsprintfW(nm, L"AtokPrivateNamespace\\JsMmf_ATOK_Shared_CommonMemory_%s_%s_%d", vers[v], suffix, slot);
            scan_mapping_strings(nm, tag);
        }
    }
    scan_mapping_strings(L"AtokPrivateNamespace\\JsMmfAtok31wDecHisSet", tag);
    scan_mapping_strings(L"AtokPrivateNamespace\\JsMmfAtok31wDC2", tag);
    /* The dictionary/engine server mappings — the lookup RESULT (candidate
     * surfaces) is marshaled through these if it lives in shared memory at all.
     * The CE/Shared/Common scans above only ever showed the ROMATABLE; check the
     * OM dictionary-server buffer, the IB exec buffer, and the DN2 model I/O. */
    scan_mapping_strings(L"AtokPrivateNamespace\\JsMmfMrpAT2db", tag);
    scan_mapping_strings(L"AtokPrivateNamespace\\JsMmfMrpAT2Db.10", tag);
    for (v = 0; v < sizeof(vers) / sizeof(vers[0]); v++) {
        wsprintfW(nm, L"AtokPrivateNamespace\\JsMmf_ATOK31IB_EXEC_DATA_%s", suffix);
        scan_mapping_strings(nm, tag);
        wsprintfW(nm, L"AtokPrivateNamespace\\JsMmf_ATOK_Shared_DN2_%s_%s", vers[v], suffix);
        scan_mapping_strings(nm, tag);
    }
}

/* Read ATOK's candidate list through the IMM/CUAS bridge. ATOK draws its own
 * candidate window, but a traditional IME still publishes the list into the
 * input context, where ImmGetCandidateList can read it — count, current
 * selection, page, and every candidate string. This is the most likely place
 * the full candidate list is actually reachable. Gated by AT_SCAN_CE. */
void dump_imm_candidates(HWND hwnd, const char *tag)
{
    HIMC himc;
    DWORD list_count = 0, total, buf_len, i;
    BYTE *buf;
    LPCANDIDATELIST cl;
    char line[600];

    if (!env_flag_enabled(L"AT_SCAN_CE")) return;
    himc = ImmGetContext(hwnd);
    if (!himc) { log_str("IMM cand: no himc\r\n"); return; }

    total = ImmGetCandidateListCountW(himc, &list_count);
    wsprintfA(line, "=== IMM cand [%s] lists=%lu total_bytes=%lu ===\r\n",
              tag, (unsigned long)list_count, (unsigned long)total);
    log_str(line);

    buf_len = ImmGetCandidateListW(himc, 0, NULL, 0);
    if (buf_len == 0) {
        log_str("  IMM cand: list 0 empty\r\n");
        ImmReleaseContext(hwnd, himc);
        return;
    }
    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, buf_len);
    if (!buf) { ImmReleaseContext(hwnd, himc); return; }
    if (ImmGetCandidateListW(himc, 0, (LPCANDIDATELIST)buf, buf_len)) {
        cl = (LPCANDIDATELIST)buf;
        wsprintfA(line, "  IMM cand: count=%lu sel=%lu pageStart=%lu pageSize=%lu\r\n",
                  (unsigned long)cl->dwCount, (unsigned long)cl->dwSelection,
                  (unsigned long)cl->dwPageStart, (unsigned long)cl->dwPageSize);
        log_str(line);
        for (i = 0; i < cl->dwCount && i < 64; i++) {
            WCHAR *s = (WCHAR *)(buf + cl->dwOffset[i]);
            int n;
            wsprintfA(line, "  imm cand[%lu]%s= ", (unsigned long)i,
                      (i == cl->dwSelection) ? "*" : "");
            log_str(line);
            n = WideCharToMultiByte(CP_UTF8, 0, s, -1, line, (int)sizeof(line) - 1, 0, 0);
            if (n > 0) log_str(line);
            log_str("\r\n");
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    ImmReleaseContext(hwnd, himc);
}
