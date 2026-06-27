#ifndef AT_RUNTIME_H
#define AT_RUNTIME_H

/* Shared surface between AtTipLoad.c (TSF host + runtime dispatcher) and the
 * extracted probe modules (candidates.c). Globals/helpers are DEFINED in
 * AtTipLoad.c and declared extern here; probe entry points are defined in
 * candidates.c and declared here so the host/dispatcher can call them. */

#include <windows.h>
#include <objbase.h>
#include <msctf.h>

/* --- shared globals (defined in AtTipLoad.c) --- */
extern ITfThreadMgr *g_tm;          /* active TSF thread manager */
extern TfClientId g_client_id;      /* our registered client id */
extern const CLSID CLSID_ATOK_TIP;  /* ATOK TIP CLSID */
/* Runtime-command override for the reconversion select index (set by
 * `reconv <n>`); 0xFFFFFFFF = fall back to the AT_RECONV_SELECT env. */
extern DWORD g_reconv_select;

/* --- daemon-mode candidate capture (defined in candidates.c) ---
 * When g_daemon_cap_active is set, the reconversion enumerator copies each
 * candidate's UTF-8 string into g_daemon_cap[] so the `convert` runtime
 * command can emit a structured result block to stdout. */
#define AT_CAP_MAX   64
#define AT_CAP_BYTES 256
extern int  g_daemon_cap_active;
extern int  g_daemon_cap_count;
extern char g_daemon_cap[AT_CAP_MAX][AT_CAP_BYTES];
/* Emit the captured candidates as an ATD result block on stdout (defined in
 * AtTipLoad.c). Called from the reconversion enumerator BEFORE ATOK's
 * edit-session teardown, which can fault on a long-lived process — so the
 * result reaches the relay even if the teardown then crashes. */
void at_daemon_emit_result(void);
/* Phase-timing probe (AT_TIPLOAD_TIMING): emit `ATD MS <label> <elapsed_ms>`.
 * No-op unless timing is enabled. Defined in AtTipLoad.c. */
void at_daemon_ms(const char *label);

/* --- shared logging / env helpers (defined in AtTipLoad.c; the file-aware
 * logger, so probe output reaches tipruntime.log — distinct from textstore.c's
 * private stdout-only log_str) --- */
void log_str(const char *s);
DWORD get_env_dword(const wchar_t *name, DWORD fallback);
BOOL env_flag_enabled(const wchar_t *name);

/* --- candidate / reconversion / function-provider probes (defined in
 * candidates.c) --- */
void probe_function_provider(ITfFunctionProvider *prov);
void probe_search_candidates(void);
/* Daemon fast path: ITfFnSearchCandidateProvider::GetSearchCandidates(query) —
 * no edit session, no commit, no teardown. Captures candidates into
 * g_daemon_cap[] and emits the ATD block. Defined in candidates.c. */
void search_candidates_daemon(const WCHAR *query);
void probe_reconversion(ITfContext *context, const char *tag, BOOL force);
void scan_heap_comment_markers(const char *tag);

/* --- CE shared-memory + IMM candidate probes (defined in ce_probe.c) --- */
void load_registry_names(wchar_t *machine, int mlen, wchar_t *ver, int vlen);
void log_ce_snapshot(const char *tag);
void scan_ce_strings(const char *tag);
void dump_imm_candidates(HWND hwnd, const char *tag);

#endif /* AT_RUNTIME_H */
