/*
 * AppInit-loaded shim for Wine.
 *
 * ATOK's TIP expects private-namespace APIs to exist and to gate access to
 * the conversion shared objects. Wine stubs those APIs, so we emulate just
 * enough of the namespace contract for ATOK to proceed:
 *   - fake boundary/namespace handles
 *   - rewrite "AtokPrivateNamespace\\<name>" to a flat object name
 *   - patch ATOK module imports in-process
 */

#include <windows.h>
#include <tlhelp32.h>

#define AT_NS_PREFIX_W L"AtokPrivateNamespace\\"
#define AT_NS_PREFIX_A "AtokPrivateNamespace\\"
#define AT_NS_TAG_W L"ATOKNS_"

typedef HANDLE (WINAPI *PFN_CreateBoundaryDescriptorW)(LPCWSTR, ULONG);
typedef BOOL (WINAPI *PFN_AddSIDToBoundaryDescriptor)(HANDLE *, PSID);
typedef BOOL (WINAPI *PFN_DeleteBoundaryDescriptor)(HANDLE);
typedef HANDLE (WINAPI *PFN_CreatePrivateNamespaceW)(LPSECURITY_ATTRIBUTES, LPVOID, LPCWSTR);
typedef HANDLE (WINAPI *PFN_OpenPrivateNamespaceW)(LPVOID, LPCWSTR);
typedef BOOL (WINAPI *PFN_ClosePrivateNamespace)(HANDLE, ULONG);
typedef HANDLE (WINAPI *PFN_CreateFileMappingW)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
typedef HANDLE (WINAPI *PFN_OpenFileMappingW)(DWORD, BOOL, LPCWSTR);
typedef HANDLE (WINAPI *PFN_CreateFileMappingA)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
typedef HANDLE (WINAPI *PFN_OpenFileMappingA)(DWORD, BOOL, LPCSTR);
typedef LPVOID (WINAPI *PFN_MapViewOfFile)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
typedef BOOL (WINAPI *PFN_UnmapViewOfFile)(LPCVOID);
typedef HANDLE (WINAPI *PFN_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *PFN_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef DWORD (WINAPI *PFN_GetFileAttributesW)(LPCWSTR);
typedef DWORD (WINAPI *PFN_GetFileAttributesA)(LPCSTR);
typedef HANDLE (WINAPI *PFN_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef HANDLE (WINAPI *PFN_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
typedef HMODULE (WINAPI *PFN_LoadLibraryW)(LPCWSTR);
typedef HMODULE (WINAPI *PFN_LoadLibraryA)(LPCSTR);
typedef HMODULE (WINAPI *PFN_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);
typedef HMODULE (WINAPI *PFN_LoadLibraryExA)(LPCSTR, HANDLE, DWORD);
typedef HANDLE (WINAPI *PFN_CreateMutexW)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
typedef HANDLE (WINAPI *PFN_OpenMutexW)(DWORD, BOOL, LPCWSTR);
typedef HANDLE (WINAPI *PFN_CreateEventW)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
typedef HANDLE (WINAPI *PFN_OpenEventW)(DWORD, BOOL, LPCWSTR);
typedef HANDLE (WINAPI *PFN_CreateMutexA)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
typedef HANDLE (WINAPI *PFN_OpenMutexA)(DWORD, BOOL, LPCSTR);
typedef HANDLE (WINAPI *PFN_CreateEventA)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
typedef HANDLE (WINAPI *PFN_OpenEventA)(DWORD, BOOL, LPCSTR);
typedef DWORD (WINAPI *PFN_SetNamedSecurityInfoW)(LPWSTR, DWORD, SECURITY_INFORMATION, PSID, PSID, PACL, PACL);
typedef DWORD (WINAPI *PFN_GetNamedSecurityInfoW)(LPWSTR, DWORD, SECURITY_INFORMATION, PSID *, PSID *, PACL *, PACL *, PSECURITY_DESCRIPTOR *);
typedef SIZE_T (WINAPI *PFN_VirtualQuery)(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T);
typedef LONG NTSTATUS_SHIM;
typedef struct ShimUnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} ShimUnicodeString;
typedef struct ShimObjectAttributes {
    ULONG Length;
    HANDLE RootDirectory;
    ShimUnicodeString *ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} ShimObjectAttributes;
typedef NTSTATUS_SHIM (WINAPI *PFN_NtCreateSection)(PHANDLE, ACCESS_MASK, PVOID, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
typedef NTSTATUS_SHIM (WINAPI *PFN_NtOpenSection)(PHANDLE, ACCESS_MASK, PVOID);
typedef NTSTATUS_SHIM (WINAPI *PFN_NtCreateMutant)(PHANDLE, ACCESS_MASK, PVOID, BOOLEAN);
typedef NTSTATUS_SHIM (WINAPI *PFN_NtOpenMutant)(PHANDLE, ACCESS_MASK, PVOID);
typedef LONG (WINAPI *PFN_RegOpenKeyW)(HKEY, LPCWSTR, PHKEY);
typedef LONG (WINAPI *PFN_RegOpenKeyExW)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *PFN_RegOpenKeyExA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *PFN_RegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *PFN_RegQueryValueExA)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);

typedef struct FakeHandle {
    DWORD magic;
    DWORD kind;
    WCHAR name[128];
} FakeHandle;

enum {
    FAKE_KIND_BOUNDARY = 1,
    FAKE_KIND_NAMESPACE = 2,
};

static PFN_CreateBoundaryDescriptorW pCreateBoundaryDescriptorW;
static PFN_AddSIDToBoundaryDescriptor pAddSIDToBoundaryDescriptor;
static PFN_DeleteBoundaryDescriptor pDeleteBoundaryDescriptor;
static PFN_CreatePrivateNamespaceW pCreatePrivateNamespaceW;
static PFN_OpenPrivateNamespaceW pOpenPrivateNamespaceW;
static PFN_ClosePrivateNamespace pClosePrivateNamespace;
static PFN_CreateFileMappingW pCreateFileMappingW;
static PFN_OpenFileMappingW pOpenFileMappingW;
static PFN_CreateFileMappingA pCreateFileMappingA;
static PFN_OpenFileMappingA pOpenFileMappingA;
static PFN_MapViewOfFile pMapViewOfFile;
static PFN_UnmapViewOfFile pUnmapViewOfFile;
static PFN_CreateFileW pCreateFileW;
static PFN_CreateFileA pCreateFileA;
static PFN_GetFileAttributesW pGetFileAttributesW;
static PFN_GetFileAttributesA pGetFileAttributesA;
static PFN_FindFirstFileW pFindFirstFileW;
static PFN_FindFirstFileA pFindFirstFileA;
static PFN_LoadLibraryW pLoadLibraryW;
static PFN_LoadLibraryA pLoadLibraryA;
static PFN_LoadLibraryExW pLoadLibraryExW;
static PFN_LoadLibraryExA pLoadLibraryExA;
static PFN_CreateMutexW pCreateMutexW;
static PFN_OpenMutexW pOpenMutexW;
static PFN_CreateEventW pCreateEventW;
static PFN_OpenEventW pOpenEventW;
static PFN_CreateMutexA pCreateMutexA;
static PFN_OpenMutexA pOpenMutexA;
static PFN_CreateEventA pCreateEventA;
static PFN_OpenEventA pOpenEventA;
static PFN_SetNamedSecurityInfoW pSetNamedSecurityInfoW;
static PFN_GetNamedSecurityInfoW pGetNamedSecurityInfoW;
typedef void (WINAPI *PFN_RaiseException)(DWORD, DWORD, DWORD, const ULONG_PTR *);
static PFN_RaiseException pRaiseException;
static PFN_VirtualQuery pVirtualQuery;
static PFN_NtCreateSection pNtCreateSection;
static PFN_NtOpenSection pNtOpenSection;
static PFN_NtCreateMutant pNtCreateMutant;
static PFN_NtOpenMutant pNtOpenMutant;
static PFN_RegOpenKeyW pRegOpenKeyW;
static PFN_RegOpenKeyExW pRegOpenKeyExW;
static PFN_RegOpenKeyExA pRegOpenKeyExA;
static PFN_RegQueryValueExW pRegQueryValueExW;
static PFN_RegQueryValueExA pRegQueryValueExA;

static volatile LONG g_init_state;
static HANDLE g_worker_thread;
static DWORD g_worker_tid;
static HANDLE g_autocreated_ce_map;
static HANDLE g_autocreated_ib_map;
static HANDLE g_precreated_dn2_map;
static HANDLE g_precreated_dn4_map;

#define MAP_NAME_SLOTS 64
static HANDLE g_map_handles[MAP_NAME_SLOTS];
static WCHAR g_map_names[MAP_NAME_SLOTS][128];
static HMODULE g_patch_scan_logged[MAP_NAME_SLOTS];
static HKEY g_reg_handles[MAP_NAME_SLOTS];
static WCHAR g_reg_paths[MAP_NAME_SLOTS][260];

static void patch_known_atok_modules(void);
static int wcs_contains_i(const WCHAR *s, const WCHAR *needle);
static int str_contains_i(const char *s, const char *needle);
static int env_flag_enabled_w(const WCHAR *name);

static int c_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void log_line(const char *s)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (out) {
        WriteFile(out, s, (DWORD)c_strlen(s), &written, 0);
    }
}

static void log_line2(const char *a, const char *b)
{
    log_line(a);
    log_line(b);
    log_line("\r\n");
}

static void log_dword_dec(DWORD v)
{
    char buf[16];
    int i = 0;
    int j;
    if (v == 0) {
        log_line("0");
        return;
    }
    while (v && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (j = i - 1; j >= 0; j--) {
        char c[2];
        c[0] = buf[j];
        c[1] = 0;
        log_line(c);
    }
}

static void log_hex32(DWORD v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xf];
    }
    buf[10] = 0;
    log_line(buf);
}

static void log_ptr_hex(const void *p)
{
    ULONG_PTR v = (ULONG_PTR)p;
    static const char hex[] = "0123456789ABCDEF";
    char buf[2 + sizeof(ULONG_PTR) * 2 + 1];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < (int)(sizeof(ULONG_PTR) * 2); i++) {
        int shift = (int)((sizeof(ULONG_PTR) * 2 - 1 - i) * 4);
        buf[2 + i] = hex[(v >> shift) & 0xf];
    }
    buf[2 + sizeof(ULONG_PTR) * 2] = 0;
    log_line(buf);
}

static void log_ascii_probe(const char *tag, const unsigned char *p, DWORD n)
{
    DWORD i;
    DWORD run = 0;
    if (!p) return;
    log_line(tag);
    for (i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (c >= 0x20 && c <= 0x7e) {
            char s[2];
            if (!run) log_line("\"");
            s[0] = (char)c;
            s[1] = 0;
            log_line(s);
            run++;
            if (run >= 96) break;
        } else {
            if (run >= 6) {
                log_line("\"\r\n");
                return;
            }
            if (run) {
                log_line(" ");
                run = 0;
            }
        }
    }
    if (run) log_line("\"\r\n");
    else log_line("(no printable run)\r\n");
}

static int is_readable_range(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    ULONG_PTR start = (ULONG_PTR)p;
    ULONG_PTR end = start + n;
    DWORD bad;
    if (!p || !n || end < start || !pVirtualQuery) return 0;
    if (pVirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    bad = PAGE_NOACCESS | PAGE_GUARD;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & bad)) return 0;
    return end <= ((ULONG_PTR)mbi.BaseAddress + mbi.RegionSize);
}

static void log_exception_object_slots(const DWORD *obj)
{
    int i;
    if (!is_readable_range(obj, sizeof(DWORD) * 8)) return;
    for (i = 0; i < 8; i++) {
        DWORD v = obj[i];
        log_line("AtNsShim: C++ object[");
        log_dword_dec((DWORD)i);
        log_line("]=");
        log_hex32(v);
        log_line("\r\n");
        if (v >= 0x10000 && is_readable_range((const void *)(ULONG_PTR)v, 96)) {
            log_ascii_probe("AtNsShim: C++ object ptr ascii=", (const unsigned char *)(ULONG_PTR)v, 96);
        }
    }
}

/* Log a named-object call with its (wide) name and HANDLE result, so we can
 * see exactly which backend object ATOK opens and whether it failed (NULL).
 * kernel32-only (no wsprintf, which lives in user32). */
static void log_obj_w(const char *fn, const WCHAR *name, HANDLE result)
{
    char nm[256];
    int i = 0;
    if (name) {
        while (name[i] && i < (int)sizeof(nm) - 1) { nm[i] = (char)(name[i] & 0x7f); i++; }
    }
    nm[i] = 0;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" name=\"");
    log_line(nm);
    log_line(result ? "\" -> OK\r\n" : "\" -> NULL\r\n");
}

static const WCHAR *nt_object_name(PVOID object_attributes)
{
    ShimObjectAttributes *oa = (ShimObjectAttributes *)object_attributes;
    if (!oa || !oa->ObjectName || !oa->ObjectName->Buffer || oa->ObjectName->Length == 0) {
        return NULL;
    }
    return oa->ObjectName->Buffer;
}

static void log_nt_obj(const char *fn, PVOID object_attributes, NTSTATUS_SHIM status, HANDLE result)
{
    const WCHAR *name = nt_object_name(object_attributes);
    char nm[256];
    int i = 0;
    int max;
    if (!name) return;
    max = ((ShimObjectAttributes *)object_attributes)->ObjectName->Length / (int)sizeof(WCHAR);
    while (i < max && name[i] && i < (int)sizeof(nm) - 1) {
        WCHAR ch = name[i];
        nm[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
        i++;
    }
    nm[i] = 0;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" name=\"");
    log_line(nm);
    log_line("\" status=");
    log_hex32((DWORD)status);
    log_line(" handle=");
    log_ptr_hex(result);
    log_line("\r\n");
}

static int remember_patch_scan(HMODULE mod)
{
    int i;
    int slot = -1;
    if (!mod) return 0;
    for (i = 0; i < MAP_NAME_SLOTS; i++) {
        if (g_patch_scan_logged[i] == mod) return 0;
        if (!g_patch_scan_logged[i] && slot < 0) slot = i;
    }
    if (slot < 0) return 0;
    g_patch_scan_logged[slot] = mod;
    return 1;
}

static void remember_map_name(HANDLE h, const WCHAR *name)
{
    int i;
    int slot = -1;
    if (!h || !name) return;
    for (i = 0; i < MAP_NAME_SLOTS; i++) {
        if (g_map_handles[i] == h) {
            slot = i;
            break;
        }
        if (!g_map_handles[i] && slot < 0) slot = i;
    }
    if (slot < 0) slot = 0;
    g_map_handles[slot] = h;
    lstrcpynW(g_map_names[slot], name, 128);
}

static const WCHAR *lookup_map_name(HANDLE h)
{
    int i;
    for (i = 0; i < MAP_NAME_SLOTS; i++) {
        if (g_map_handles[i] == h) return g_map_names[i];
    }
    return L"(unknown)";
}

static const WCHAR *predefined_reg_name(HKEY h)
{
    if (h == HKEY_CLASSES_ROOT) return L"HKEY_CLASSES_ROOT";
    if (h == HKEY_CURRENT_USER) return L"HKEY_CURRENT_USER";
    if (h == HKEY_LOCAL_MACHINE) return L"HKEY_LOCAL_MACHINE";
    if (h == HKEY_USERS) return L"HKEY_USERS";
    if (h == HKEY_CURRENT_CONFIG) return L"HKEY_CURRENT_CONFIG";
    return NULL;
}

static const WCHAR *lookup_reg_path(HKEY h)
{
    int i;
    const WCHAR *predef = predefined_reg_name(h);
    if (predef) return predef;
    for (i = 0; i < MAP_NAME_SLOTS; i++) {
        if (g_reg_handles[i] == h) return g_reg_paths[i];
    }
    return L"(unknown)";
}

static void append_w(WCHAR *dst, int cch, const WCHAR *src)
{
    int i;
    int j = 0;
    if (!dst || cch <= 0 || !src) return;
    i = lstrlenW(dst);
    while (src[j] && i + 1 < cch) dst[i++] = src[j++];
    dst[i] = 0;
}

static void build_reg_path_w(HKEY root, const WCHAR *subkey, WCHAR *out, int cch)
{
    const WCHAR *base = lookup_reg_path(root);
    if (!out || cch <= 0) return;
    out[0] = 0;
    lstrcpynW(out, base ? base : L"(unknown)", cch);
    if (subkey && *subkey) {
        append_w(out, cch, L"\\");
        append_w(out, cch, subkey);
    }
}

static void remember_reg_path(HKEY h, const WCHAR *path)
{
    int i;
    int slot = -1;
    if (!h || !path) return;
    for (i = 0; i < MAP_NAME_SLOTS; i++) {
        if (g_reg_handles[i] == h) {
            slot = i;
            break;
        }
        if (!g_reg_handles[i] && slot < 0) slot = i;
    }
    if (slot < 0) slot = 0;
    g_reg_handles[slot] = h;
    lstrcpynW(g_reg_paths[slot], path, 260);
}

static int reg_trace_interesting_w(const WCHAR *path, const WCHAR *value)
{
    return wcs_contains_i(path, L"Justsystem") ||
           wcs_contains_i(path, L"ATOK") ||
           wcs_contains_i(path, L"ExtraTransEngine") ||
           wcs_contains_i(value, L"CheckLibraryExist") ||
           wcs_contains_i(value, L"ATOKDN") ||
           wcs_contains_i(value, L"ATOKDC") ||
           wcs_contains_i(value, L"DN") ||
           wcs_contains_i(value, L"Model");
}

static void log_wide_ascii(const WCHAR *s, int cch_limit)
{
    char buf[512];
    int i = 0;
    if (s) {
        while (s[i] && i < cch_limit && i < (int)sizeof(buf) - 1) {
            WCHAR ch = s[i];
            buf[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
            i++;
        }
    }
    buf[i] = 0;
    log_line(buf);
}

static void log_reg_open_w(const char *fn, const WCHAR *path, LONG status, HKEY result)
{
    if (!env_flag_enabled_w(L"AT_TRACE_REG") || !reg_trace_interesting_w(path, NULL)) return;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" key=\"");
    log_wide_ascii(path, 511);
    log_line("\" status=");
    log_hex32((DWORD)status);
    log_line(" handle=");
    log_ptr_hex(result);
    log_line("\r\n");
}

static void log_reg_query_w(const char *fn, HKEY hKey, const WCHAR *value_name,
                            LONG status, DWORD type, const BYTE *data, DWORD cb)
{
    const WCHAR *path = lookup_reg_path(hKey);
    if (!env_flag_enabled_w(L"AT_TRACE_REG") || !reg_trace_interesting_w(path, value_name)) return;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" key=\"");
    log_wide_ascii(path, 511);
    log_line("\" value=\"");
    log_wide_ascii(value_name ? value_name : L"(default)", 160);
    log_line("\" status=");
    log_hex32((DWORD)status);
    log_line(" type=");
    log_dword_dec(type);
    log_line(" cb=");
    log_dword_dec(cb);
    if (status == ERROR_SUCCESS && data) {
        if (type == REG_DWORD && cb >= 4) {
            DWORD v = *(const DWORD *)data;
            log_line(" data=");
            log_hex32(v);
        } else if ((type == REG_SZ || type == REG_EXPAND_SZ) && cb >= sizeof(WCHAR)) {
            log_line(" data=\"");
            log_wide_ascii((const WCHAR *)data, (int)(cb / sizeof(WCHAR)));
            log_line("\"");
        }
    }
    log_line("\r\n");
}

static void log_map_view(HANDLE hFileMappingObject, LPVOID view)
{
    const WCHAR *name = lookup_map_name(hFileMappingObject);
    log_line("AtNsShim: MapViewOfFile handle=");
    log_hex32((DWORD)(ULONG_PTR)hFileMappingObject);
    log_line(" name=\"");
    {
        char nm[128];
        int i = 0;
        while (name && name[i] && i < (int)sizeof(nm) - 1) { nm[i] = (char)(name[i] & 0x7f); i++; }
        nm[i] = 0;
        log_line(nm);
    }
    log_line("\" -> ");
    if (view) {
        log_hex32((DWORD)(ULONG_PTR)view);
        log_line("\r\n");
    } else {
        log_line("NULL\r\n");
    }
}

static int wcs_contains_i(const WCHAR *s, const WCHAR *needle)
{
    const WCHAR *p;
    if (!s || !needle || !*needle) return 0;
    for (p = s; *p; p++) {
        const WCHAR *a = p;
        const WCHAR *b = needle;
        while (*a && *b) {
            WCHAR ca = *a;
            WCHAR cb = *b;
            if (ca >= L'A' && ca <= L'Z') ca = (WCHAR)(ca - L'A' + L'a');
            if (cb >= L'A' && cb <= L'Z') cb = (WCHAR)(cb - L'A' + L'a');
            if (ca != cb) break;
            a++;
            b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static int str_contains_i(const char *s, const char *needle)
{
    const char *p;
    if (!s || !needle || !*needle) return 0;
    for (p = s; *p; p++) {
        const char *a = p;
        const char *b = needle;
        while (*a && *b) {
            char ca = *a;
            char cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) break;
            a++;
            b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static void log_address_site(const char *tag, void *addr)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!addr) return;
    if (pVirtualQuery && pVirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        WCHAR path[260];
        char path_a[260];
        DWORD i = 0;
        DWORD got;
        HMODULE mod = (HMODULE)mbi.AllocationBase;
        path[0] = 0;
        path_a[0] = 0;
        got = GetModuleFileNameW(mod, path, (DWORD)(sizeof(path) / sizeof(path[0])));
        if (got) {
            while (path[i] && i < (DWORD)sizeof(path_a) - 1) {
                WCHAR ch = path[i];
                path_a[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
                i++;
            }
            path_a[i] = 0;
        }
        log_line("AtNsShim: ");
        log_line(tag);
        log_line(" addr=");
        log_ptr_hex(addr);
        log_line(" module=");
        log_line(path_a[0] ? path_a : "(unknown)");
        log_line(" offset=");
        log_ptr_hex((const void *)((const unsigned char *)addr - (const unsigned char *)mod));
        log_line("\r\n");
    }
}

static void log_file_w(const char *fn, const WCHAR *name, int failed, DWORD err)
{
    char nm[512];
    int i = 0;
    int interesting = failed ||
        wcs_contains_i(name, L"ATOK") ||
        wcs_contains_i(name, L"Justsystem") ||
        wcs_contains_i(name, L"JustSystems");
    if (!interesting) return;
    if (name) {
        while (name[i] && i < (int)sizeof(nm) - 1) {
            WCHAR ch = name[i];
            nm[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
            i++;
        }
    }
    nm[i] = 0;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" path=\"");
    log_line(nm);
    if (failed) {
        log_line("\" -> FAIL err=");
        log_dword_dec(err);
        log_line("\r\n");
    } else {
        log_line("\" -> OK\r\n");
    }
}

static void log_file_a(const char *fn, const char *name, int failed, DWORD err)
{
    char nm[512];
    int i = 0;
    int interesting = failed ||
        str_contains_i(name, "ATOK") ||
        str_contains_i(name, "Justsystem") ||
        str_contains_i(name, "JustSystems");
    if (!interesting) return;
    if (name) {
        while (name[i] && i < (int)sizeof(nm) - 1) {
            nm[i] = (name[i] >= 0x20 && name[i] < 0x7f) ? name[i] : '?';
            i++;
        }
    }
    nm[i] = 0;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" path=\"");
    log_line(nm);
    if (failed) {
        log_line("\" -> FAIL err=");
        log_dword_dec(err);
        log_line("\r\n");
    } else {
        log_line("\" -> OK\r\n");
    }
}

static void log_load_w(const char *fn, const WCHAR *name, HMODULE result, DWORD err)
{
    char nm[512];
    int i = 0;
    int interesting =
        wcs_contains_i(name, L"ATOK") ||
        wcs_contains_i(name, L"Justsystem") ||
        wcs_contains_i(name, L"JustSystems");
    if (!interesting) return;
    if (name) {
        while (name[i] && i < (int)sizeof(nm) - 1) {
            WCHAR ch = name[i];
            nm[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
            i++;
        }
    }
    nm[i] = 0;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" module=\"");
    log_line(nm);
    if (result) {
        log_line("\" -> OK\r\n");
    } else {
        log_line("\" -> NULL err=");
        log_dword_dec(err);
        log_line("\r\n");
    }
}

static void log_load_a(const char *fn, const char *name, HMODULE result, DWORD err)
{
    char nm[512];
    int i = 0;
    int interesting =
        str_contains_i(name, "ATOK") ||
        str_contains_i(name, "Justsystem") ||
        str_contains_i(name, "JustSystems");
    if (!interesting) return;
    if (name) {
        while (name[i] && i < (int)sizeof(nm) - 1) {
            nm[i] = (name[i] >= 0x20 && name[i] < 0x7f) ? name[i] : '?';
            i++;
        }
    }
    nm[i] = 0;
    log_line("AtNsShim: ");
    log_line(fn);
    log_line(" module=\"");
    log_line(nm);
    if (result) {
        log_line("\" -> OK\r\n");
    } else {
        log_line("\" -> NULL err=");
        log_dword_dec(err);
        log_line("\r\n");
    }
}

static int env_flag_enabled_w(const WCHAR *name)
{
    WCHAR buf[8];
    return GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0]))) > 0 &&
           buf[0] != L'0';
}

static void env_or_default_path(WCHAR *dst, DWORD dstc, const WCHAR *env_name, const WCHAR *fallback)
{
    DWORD got;
    if (!dst || !dstc) return;
    dst[0] = 0;
    got = GetEnvironmentVariableW(env_name, dst, dstc);
    if (!got || got >= dstc) {
        lstrcpynW(dst, fallback, (int)dstc);
    }
}

static void log_precreate_read(const char *label, const WCHAR *path, DWORD got, DWORD expected)
{
    char nm[512];
    int i = 0;
    if (path) {
        while (path[i] && i < (int)sizeof(nm) - 1) {
            WCHAR ch = path[i];
            nm[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
            i++;
        }
    }
    nm[i] = 0;
    log_line("AtNsShim: ");
    log_line(label);
    log_line(" snapshot bytes=");
    log_dword_dec(got);
    log_line("/");
    log_dword_dec(expected);
    log_line(" path=\"");
    log_line(nm);
    log_line("\"\r\n");
}

static void fill_map_from_snapshot(HANDLE map, const char *label, const WCHAR *env_name,
                                   const WCHAR *fallback_path, DWORD bytes)
{
    WCHAR path[260];
    HANDLE f;
    unsigned char *view;
    DWORD total = 0;
    if (!map || !pCreateFileW || !pMapViewOfFile || !pUnmapViewOfFile) return;
    env_or_default_path(path, (DWORD)(sizeof(path) / sizeof(path[0])), env_name, fallback_path);
    f = pCreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) {
        log_file_w(label, path, 1, GetLastError());
        return;
    }
    view = (unsigned char *)pMapViewOfFile(map, FILE_MAP_WRITE, 0, 0, bytes);
    if (!view) {
        log_line("AtNsShim: ");
        log_line(label);
        log_line(" MapViewOfFile failed err=");
        log_dword_dec(GetLastError());
        log_line("\r\n");
        CloseHandle(f);
        return;
    }
    while (total < bytes) {
        DWORD chunk = bytes - total;
        DWORD got = 0;
        if (chunk > 0x10000) chunk = 0x10000;
        if (!ReadFile(f, view + total, chunk, &got, 0) || got == 0) break;
        total += got;
    }
    pUnmapViewOfFile(view);
    CloseHandle(f);
    log_precreate_read(label, path, total, bytes);
}

static void precreate_dn_maps_if_requested(void)
{
    static volatile LONG attempted;
    static const DWORD dn_model_bytes = 0x00953000;
    static const WCHAR dn2_name[] = L"JsMmf_ATOK_Shared_DN2_31.2.5";
    static const WCHAR dn4_name[] = L"JsMmf_ATOK_Shared_DN4_31.2.5";
    static const WCHAR dn2_default[] =
        L"Z:\\workspaces\\atok\\recon\\captures\\host-dn-full-20260614-host-dn-full_JsMmf_ATOK_Shared_DN2_31.2.5.bin";
    static const WCHAR dn4_default[] =
        L"Z:\\workspaces\\atok\\recon\\captures\\host-dn-full-20260614-host-dn-full_JsMmf_ATOK_Shared_DN4_31.2.5.bin";

    if (!env_flag_enabled_w(L"AT_PRECREATE_DN")) return;
    if (InterlockedCompareExchange(&attempted, 1, 0) != 0) return;
    if (!pCreateFileMappingW) return;

    g_precreated_dn2_map = pCreateFileMappingW(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE,
                                               0, dn_model_bytes, dn2_name);
    remember_map_name(g_precreated_dn2_map, dn2_name);
    log_obj_w("PrecreateDN2", dn2_name, g_precreated_dn2_map);
    fill_map_from_snapshot(g_precreated_dn2_map, "PrecreateDN2", L"AT_DN2_SNAPSHOT",
                           dn2_default, dn_model_bytes);

    g_precreated_dn4_map = pCreateFileMappingW(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE,
                                               0, dn_model_bytes, dn4_name);
    remember_map_name(g_precreated_dn4_map, dn4_name);
    log_obj_w("PrecreateDN4", dn4_name, g_precreated_dn4_map);
    fill_map_from_snapshot(g_precreated_dn4_map, "PrecreateDN4", L"AT_DN4_SNAPSHOT",
                           dn4_default, dn_model_bytes);
}

static void seed_ce_view(unsigned char *p)
{
    if (!p) return;
    p[0] = 1;
    p[1] = 0;
    p[0x72FE] = 0x18;
    p[0x72FF] = 0x00;
    p[0x737E] = 0x27;
    p[0x737F] = 0x00;
    p[0x74CE] = 0x17;
    p[0x74CF] = 0x00;
}

static int wcs_eqi(const WCHAR *a, const WCHAR *b)
{
    while (*a && *b) {
        WCHAR ca = *a;
        WCHAR cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca = (WCHAR)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (WCHAR)(cb - L'A' + L'a');
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int wcs_starts_with_i(const WCHAR *s, const WCHAR *prefix)
{
    while (*prefix) {
        WCHAR ca = *s++;
        WCHAR cb = *prefix++;
        if (!ca) return 0;
        if (ca >= L'A' && ca <= L'Z') ca = (WCHAR)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (WCHAR)(cb - L'A' + L'a');
        if (ca != cb) return 0;
    }
    return 1;
}

static void sanitize_ns_name(const WCHAR *name, WCHAR *out, DWORD out_cch)
{
    DWORD i = 0;
    const WCHAR *p = name;
    if (!out || out_cch == 0) return;
    out[0] = 0;
    lstrcpyW(out, AT_NS_TAG_W);
    i = lstrlenW(out);
    while (*p && i + 1 < out_cch) {
        WCHAR ch = *p++;
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L' ') {
            ch = L'_';
        }
        out[i++] = ch;
    }
    out[i] = 0;
}

static int is_fake_handle(HANDLE h, DWORD kind)
{
    FakeHandle *fh = (FakeHandle *)h;
    return fh && fh->magic == 0x544B4E53 && fh->kind == kind;
}

static HANDLE make_fake_handle(DWORD kind, const WCHAR *name)
{
    FakeHandle *fh = (FakeHandle *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FakeHandle));
    if (!fh) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    fh->magic = 0x544B4E53;
    fh->kind = kind;
    if (name) {
        lstrcpynW(fh->name, name, (int)(sizeof(fh->name) / sizeof(fh->name[0])));
    }
    return (HANDLE)fh;
}

static void free_fake_handle(HANDLE h)
{
    FakeHandle *fh = (FakeHandle *)h;
    if (fh && fh->magic == 0x544B4E53) {
        HeapFree(GetProcessHeap(), 0, fh);
    }
}

static void resolve_originals(void)
{
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE adv = GetModuleHandleW(L"advapi32.dll");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!k32) return;

    pCreateBoundaryDescriptorW = (PFN_CreateBoundaryDescriptorW)GetProcAddress(k32, "CreateBoundaryDescriptorW");
    pAddSIDToBoundaryDescriptor = (PFN_AddSIDToBoundaryDescriptor)GetProcAddress(k32, "AddSIDToBoundaryDescriptor");
    pDeleteBoundaryDescriptor = (PFN_DeleteBoundaryDescriptor)GetProcAddress(k32, "DeleteBoundaryDescriptor");
    pCreatePrivateNamespaceW = (PFN_CreatePrivateNamespaceW)GetProcAddress(k32, "CreatePrivateNamespaceW");
    pOpenPrivateNamespaceW = (PFN_OpenPrivateNamespaceW)GetProcAddress(k32, "OpenPrivateNamespaceW");
    pClosePrivateNamespace = (PFN_ClosePrivateNamespace)GetProcAddress(k32, "ClosePrivateNamespace");
    pCreateFileMappingW = (PFN_CreateFileMappingW)GetProcAddress(k32, "CreateFileMappingW");
    pOpenFileMappingW = (PFN_OpenFileMappingW)GetProcAddress(k32, "OpenFileMappingW");
    pCreateFileMappingA = (PFN_CreateFileMappingA)GetProcAddress(k32, "CreateFileMappingA");
    pOpenFileMappingA = (PFN_OpenFileMappingA)GetProcAddress(k32, "OpenFileMappingA");
    pMapViewOfFile = (PFN_MapViewOfFile)GetProcAddress(k32, "MapViewOfFile");
    pUnmapViewOfFile = (PFN_UnmapViewOfFile)GetProcAddress(k32, "UnmapViewOfFile");
    pCreateFileW = (PFN_CreateFileW)GetProcAddress(k32, "CreateFileW");
    pCreateFileA = (PFN_CreateFileA)GetProcAddress(k32, "CreateFileA");
    pGetFileAttributesW = (PFN_GetFileAttributesW)GetProcAddress(k32, "GetFileAttributesW");
    pGetFileAttributesA = (PFN_GetFileAttributesA)GetProcAddress(k32, "GetFileAttributesA");
    pFindFirstFileW = (PFN_FindFirstFileW)GetProcAddress(k32, "FindFirstFileW");
    pFindFirstFileA = (PFN_FindFirstFileA)GetProcAddress(k32, "FindFirstFileA");
    pLoadLibraryW = (PFN_LoadLibraryW)GetProcAddress(k32, "LoadLibraryW");
    pLoadLibraryA = (PFN_LoadLibraryA)GetProcAddress(k32, "LoadLibraryA");
    pLoadLibraryExW = (PFN_LoadLibraryExW)GetProcAddress(k32, "LoadLibraryExW");
    pLoadLibraryExA = (PFN_LoadLibraryExA)GetProcAddress(k32, "LoadLibraryExA");
    pCreateMutexW = (PFN_CreateMutexW)GetProcAddress(k32, "CreateMutexW");
    pOpenMutexW = (PFN_OpenMutexW)GetProcAddress(k32, "OpenMutexW");
    pCreateEventW = (PFN_CreateEventW)GetProcAddress(k32, "CreateEventW");
    pOpenEventW = (PFN_OpenEventW)GetProcAddress(k32, "OpenEventW");
    pCreateMutexA = (PFN_CreateMutexA)GetProcAddress(k32, "CreateMutexA");
    pOpenMutexA = (PFN_OpenMutexA)GetProcAddress(k32, "OpenMutexA");
    pCreateEventA = (PFN_CreateEventA)GetProcAddress(k32, "CreateEventA");
    pOpenEventA = (PFN_OpenEventA)GetProcAddress(k32, "OpenEventA");
    pRaiseException = (PFN_RaiseException)GetProcAddress(k32, "RaiseException");
    pVirtualQuery = (PFN_VirtualQuery)GetProcAddress(k32, "VirtualQuery");
    if (adv) {
        pSetNamedSecurityInfoW = (PFN_SetNamedSecurityInfoW)GetProcAddress(adv, "SetNamedSecurityInfoW");
        pGetNamedSecurityInfoW = (PFN_GetNamedSecurityInfoW)GetProcAddress(adv, "GetNamedSecurityInfoW");
        pRegOpenKeyW = (PFN_RegOpenKeyW)GetProcAddress(adv, "RegOpenKeyW");
        pRegOpenKeyExW = (PFN_RegOpenKeyExW)GetProcAddress(adv, "RegOpenKeyExW");
        pRegOpenKeyExA = (PFN_RegOpenKeyExA)GetProcAddress(adv, "RegOpenKeyExA");
        pRegQueryValueExW = (PFN_RegQueryValueExW)GetProcAddress(adv, "RegQueryValueExW");
        pRegQueryValueExA = (PFN_RegQueryValueExA)GetProcAddress(adv, "RegQueryValueExA");
    }
    if (ntdll) {
        pNtCreateSection = (PFN_NtCreateSection)GetProcAddress(ntdll, "NtCreateSection");
        pNtOpenSection = (PFN_NtOpenSection)GetProcAddress(ntdll, "NtOpenSection");
        pNtCreateMutant = (PFN_NtCreateMutant)GetProcAddress(ntdll, "NtCreateMutant");
        pNtOpenMutant = (PFN_NtOpenMutant)GetProcAddress(ntdll, "NtOpenMutant");
    }
}

static void ensure_init(void)
{
    if (InterlockedCompareExchange(&g_init_state, 1, 0) == 0) {
        resolve_originals();
        g_init_state = 2;
    } else {
        while (g_init_state != 2) Sleep(1);
    }
}

static int rewrite_ns_name(const WCHAR *name, WCHAR *buf, DWORD cch)
{
    if (!name) {
        return 0;
    }
    /* Only rewrite the AtokPrivateNamespace prefix (Wine stubs private
     * namespaces, so those names must be flattened to escape it). Strip the
     * prefix to the bare object name so it lands in BaseNamedObjects.
     *
     * Bare JsMmf/JsMtx/Atok names are NOT rewritten: they live in
     * BaseNamedObjects on Windows too and Wine handles them natively. Crucially,
     * ATOK31OM creates some of these via Nt-level APIs (which the shim cannot
     * intercept), so if we rewrote the W-API ones the names would DISAGREE
     * across processes/APIs and OM/TIP could never share the same object. */
    if (wcs_starts_with_i(name, AT_NS_PREFIX_W)) {
        const WCHAR *bare = name + lstrlenW(AT_NS_PREFIX_W);
        lstrcpynW(buf, bare, (int)cch);
        return 1;
    }
    return 0;
}

static HANDLE WINAPI shim_CreateBoundaryDescriptorW(LPCWSTR lpName, ULONG Flags)
{
    HANDLE h;
    ensure_init();
    log_line2("AtNsShim: CreateBoundaryDescriptorW", "");
    if (lpName && wcs_eqi(lpName, L"AtokBoundaryName")) {
        h = make_fake_handle(FAKE_KIND_BOUNDARY, lpName);
        if (h) return h;
    }
    if (pCreateBoundaryDescriptorW) {
        return pCreateBoundaryDescriptorW(lpName, Flags);
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static BOOL WINAPI shim_AddSIDToBoundaryDescriptor(HANDLE *BoundaryDescriptor, PSID RequiredSid)
{
    (void)RequiredSid;
    ensure_init();
    log_line2("AtNsShim: AddSIDToBoundaryDescriptor", "");
    if (BoundaryDescriptor && *BoundaryDescriptor && is_fake_handle(*BoundaryDescriptor, FAKE_KIND_BOUNDARY)) {
        return TRUE;
    }
    if (pAddSIDToBoundaryDescriptor) {
        return pAddSIDToBoundaryDescriptor(BoundaryDescriptor, RequiredSid);
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

static BOOL WINAPI shim_DeleteBoundaryDescriptor(HANDLE BoundaryDescriptor)
{
    ensure_init();
    log_line2("AtNsShim: DeleteBoundaryDescriptor", "");
    if (is_fake_handle(BoundaryDescriptor, FAKE_KIND_BOUNDARY)) {
        free_fake_handle(BoundaryDescriptor);
        return TRUE;
    }
    if (pDeleteBoundaryDescriptor) {
        return pDeleteBoundaryDescriptor(BoundaryDescriptor);
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

static HANDLE WINAPI shim_CreatePrivateNamespaceW(LPSECURITY_ATTRIBUTES lpPrivateNamespaceAttributes,
                                                  LPVOID lpBoundaryDescriptor,
                                                  LPCWSTR lpAliasPrefix)
{
    (void)lpPrivateNamespaceAttributes;
    ensure_init();
    log_line2("AtNsShim: CreatePrivateNamespaceW", "");
    if (lpAliasPrefix && wcs_eqi(lpAliasPrefix, L"AtokPrivateNamespace")) {
        return make_fake_handle(FAKE_KIND_NAMESPACE, lpAliasPrefix);
    }
    if (pCreatePrivateNamespaceW) {
        return pCreatePrivateNamespaceW(lpPrivateNamespaceAttributes, lpBoundaryDescriptor, lpAliasPrefix);
    }
    SetLastError(ERROR_PATH_NOT_FOUND);
    return NULL;
}

static HANDLE WINAPI shim_OpenPrivateNamespaceW(LPVOID lpBoundaryDescriptor, LPCWSTR lpAliasPrefix)
{
    ensure_init();
    log_line2("AtNsShim: OpenPrivateNamespaceW", "");
    if (lpAliasPrefix && wcs_eqi(lpAliasPrefix, L"AtokPrivateNamespace")) {
        return make_fake_handle(FAKE_KIND_NAMESPACE, lpAliasPrefix);
    }
    if (pOpenPrivateNamespaceW) {
        return pOpenPrivateNamespaceW(lpBoundaryDescriptor, lpAliasPrefix);
    }
    SetLastError(ERROR_PATH_NOT_FOUND);
    return NULL;
}

static BOOL WINAPI shim_ClosePrivateNamespace(HANDLE PrivateNamespace, ULONG Flags)
{
    (void)Flags;
    ensure_init();
    log_line2("AtNsShim: ClosePrivateNamespace", "");
    if (is_fake_handle(PrivateNamespace, FAKE_KIND_NAMESPACE)) {
        free_fake_handle(PrivateNamespace);
        return TRUE;
    }
    if (pClosePrivateNamespace) {
        return pClosePrivateNamespace(PrivateNamespace, Flags);
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

static HANDLE WINAPI shim_CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect,
                                             DWORD dwMaxSizeHigh, DWORD dwMaxSizeLow, LPCWSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    lstrcpynW(orig, lpName ? lpName : L"(null)", 256);
    if (rewrite_ns_name(lpName, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        lpName = rewritten;
    }
    if (pCreateFileMappingW) {
        h = pCreateFileMappingW(hFile, lpAttributes, flProtect, dwMaxSizeHigh, dwMaxSizeLow, lpName);
        remember_map_name(h, orig);
        log_obj_w("CreateFileMappingW", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_OpenFileMappingW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    lstrcpynW(orig, lpName ? lpName : L"(null)", 256);
    if (rewrite_ns_name(lpName, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        lpName = rewritten;
    }
    if (pOpenFileMappingW) {
        h = pOpenFileMappingW(dwDesiredAccess, bInheritHandle, lpName);
        if (!h &&
            env_flag_enabled_w(L"AT_AUTOCREATE_CE") &&
            wcs_contains_i(orig, L"JsMmf_ATOK31CE_SHARED_DATA") &&
            pCreateFileMappingW) {
            if (!g_autocreated_ce_map)
                g_autocreated_ce_map = pCreateFileMappingW(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, 0x10000, lpName);
            if (g_autocreated_ce_map && pMapViewOfFile && pUnmapViewOfFile) {
                unsigned char *view = (unsigned char *)pMapViewOfFile(g_autocreated_ce_map, FILE_MAP_WRITE, 0, 0, 0x10000);
                if (view) {
                    seed_ce_view(view);
                    pUnmapViewOfFile(view);
                }
            }
            log_obj_w("AutoCreateCE", orig, g_autocreated_ce_map);
            if (g_autocreated_ce_map)
                h = pOpenFileMappingW(dwDesiredAccess, bInheritHandle, lpName);
        }
        remember_map_name(h, orig);
        log_obj_w("OpenFileMappingW", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_CreateFileMappingA(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect,
                                             DWORD dwMaxSizeHigh, DWORD dwMaxSizeLow, LPCSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    if (!lpName || !MultiByteToWideChar(CP_ACP, 0, lpName, -1, orig, (int)(sizeof(orig) / sizeof(orig[0]))))
        lstrcpynW(orig, L"(null)", 256);
    if (lpName && MultiByteToWideChar(CP_ACP, 0, lpName, -1, rewritten, (int)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        if (rewrite_ns_name(rewritten, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
            if (pCreateFileMappingW) {
                h = pCreateFileMappingW(hFile, lpAttributes, flProtect, dwMaxSizeHigh, dwMaxSizeLow, rewritten);
                remember_map_name(h, orig);
                log_obj_w("CreateFileMappingA", orig, h);
                return h;
            }
        }
    }
    if (pCreateFileMappingA) {
        h = pCreateFileMappingA(hFile, lpAttributes, flProtect, dwMaxSizeHigh, dwMaxSizeLow, lpName);
        remember_map_name(h, orig);
        log_obj_w("CreateFileMappingA", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_OpenFileMappingA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    if (!lpName || !MultiByteToWideChar(CP_ACP, 0, lpName, -1, orig, (int)(sizeof(orig) / sizeof(orig[0]))))
        lstrcpynW(orig, L"(null)", 256);
    if (lpName && MultiByteToWideChar(CP_ACP, 0, lpName, -1, rewritten, (int)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        if (rewrite_ns_name(rewritten, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
            if (pOpenFileMappingW) {
                h = pOpenFileMappingW(dwDesiredAccess, bInheritHandle, rewritten);
                remember_map_name(h, orig);
                log_obj_w("OpenFileMappingA", orig, h);
                return h;
            }
        }
    }
    if (pOpenFileMappingA) {
        h = pOpenFileMappingA(dwDesiredAccess, bInheritHandle, lpName);
        remember_map_name(h, orig);
        log_obj_w("OpenFileMappingA", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static LPVOID WINAPI shim_MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess,
                                        DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow,
                                        SIZE_T dwNumberOfBytesToMap)
{
    LPVOID view;
    ensure_init();
    if (pMapViewOfFile) {
        view = pMapViewOfFile(hFileMappingObject, dwDesiredAccess, dwFileOffsetHigh,
                              dwFileOffsetLow, dwNumberOfBytesToMap);
        log_map_view(hFileMappingObject, view);
        return view;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                      LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                      DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HANDLE h;
    DWORD err = ERROR_CALL_NOT_IMPLEMENTED;
    ensure_init();
    if (pCreateFileW) {
        h = pCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
        if (h == INVALID_HANDLE_VALUE) err = GetLastError();
        log_file_w("CreateFileW", lpFileName, h == INVALID_HANDLE_VALUE, err);
        return h;
    }
    SetLastError(err);
    return INVALID_HANDLE_VALUE;
}

static HANDLE WINAPI shim_CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                      LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                      DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HANDLE h;
    DWORD err = ERROR_CALL_NOT_IMPLEMENTED;
    ensure_init();
    if (pCreateFileA) {
        h = pCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
        if (h == INVALID_HANDLE_VALUE) err = GetLastError();
        log_file_a("CreateFileA", lpFileName, h == INVALID_HANDLE_VALUE, err);
        return h;
    }
    SetLastError(err);
    return INVALID_HANDLE_VALUE;
}

static DWORD WINAPI shim_GetFileAttributesW(LPCWSTR lpFileName)
{
    DWORD attrs;
    DWORD err = ERROR_CALL_NOT_IMPLEMENTED;
    ensure_init();
    if (pGetFileAttributesW) {
        attrs = pGetFileAttributesW(lpFileName);
        if (attrs == INVALID_FILE_ATTRIBUTES) err = GetLastError();
        log_file_w("GetFileAttributesW", lpFileName, attrs == INVALID_FILE_ATTRIBUTES, err);
        return attrs;
    }
    SetLastError(err);
    return INVALID_FILE_ATTRIBUTES;
}

static DWORD WINAPI shim_GetFileAttributesA(LPCSTR lpFileName)
{
    DWORD attrs;
    DWORD err = ERROR_CALL_NOT_IMPLEMENTED;
    ensure_init();
    if (pGetFileAttributesA) {
        attrs = pGetFileAttributesA(lpFileName);
        if (attrs == INVALID_FILE_ATTRIBUTES) err = GetLastError();
        log_file_a("GetFileAttributesA", lpFileName, attrs == INVALID_FILE_ATTRIBUTES, err);
        return attrs;
    }
    SetLastError(err);
    return INVALID_FILE_ATTRIBUTES;
}

static HANDLE WINAPI shim_FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    HANDLE h;
    DWORD err = ERROR_CALL_NOT_IMPLEMENTED;
    ensure_init();
    if (pFindFirstFileW) {
        h = pFindFirstFileW(lpFileName, lpFindFileData);
        if (h == INVALID_HANDLE_VALUE) err = GetLastError();
        log_file_w("FindFirstFileW", lpFileName, h == INVALID_HANDLE_VALUE, err);
        return h;
    }
    SetLastError(err);
    return INVALID_HANDLE_VALUE;
}

static HANDLE WINAPI shim_FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    HANDLE h;
    DWORD err = ERROR_CALL_NOT_IMPLEMENTED;
    ensure_init();
    if (pFindFirstFileA) {
        h = pFindFirstFileA(lpFileName, lpFindFileData);
        if (h == INVALID_HANDLE_VALUE) err = GetLastError();
        log_file_a("FindFirstFileA", lpFileName, h == INVALID_HANDLE_VALUE, err);
        return h;
    }
    SetLastError(err);
    return INVALID_HANDLE_VALUE;
}

static void ansi_to_wide(const char *src, WCHAR *dst, int dstc)
{
    if (!dst || dstc <= 0) return;
    dst[0] = 0;
    if (!src) return;
    MultiByteToWideChar(CP_ACP, 0, src, -1, dst, dstc);
    dst[dstc - 1] = 0;
}

static LONG WINAPI shim_RegOpenKeyW(HKEY hKey, LPCWSTR lpSubKey, PHKEY phkResult)
{
    LONG status = ERROR_CALL_NOT_IMPLEMENTED;
    WCHAR path[260];
    ensure_init();
    build_reg_path_w(hKey, lpSubKey, path, 260);
    if (pRegOpenKeyW) {
        status = pRegOpenKeyW(hKey, lpSubKey, phkResult);
        if (status == ERROR_SUCCESS && phkResult && *phkResult) remember_reg_path(*phkResult, path);
    } else {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    }
    log_reg_open_w("RegOpenKeyW", path, status, phkResult ? *phkResult : NULL);
    return status;
}

static LONG WINAPI shim_RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions,
                                      REGSAM samDesired, PHKEY phkResult)
{
    LONG status = ERROR_CALL_NOT_IMPLEMENTED;
    WCHAR path[260];
    (void)ulOptions;
    (void)samDesired;
    ensure_init();
    build_reg_path_w(hKey, lpSubKey, path, 260);
    if (pRegOpenKeyExW) {
        status = pRegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
        if (status == ERROR_SUCCESS && phkResult && *phkResult) remember_reg_path(*phkResult, path);
    } else {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    }
    log_reg_open_w("RegOpenKeyExW", path, status, phkResult ? *phkResult : NULL);
    return status;
}

static LONG WINAPI shim_RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions,
                                      REGSAM samDesired, PHKEY phkResult)
{
    LONG status = ERROR_CALL_NOT_IMPLEMENTED;
    WCHAR subkey_w[260];
    WCHAR path[260];
    (void)ulOptions;
    (void)samDesired;
    ensure_init();
    ansi_to_wide(lpSubKey, subkey_w, 260);
    build_reg_path_w(hKey, subkey_w, path, 260);
    if (pRegOpenKeyExA) {
        status = pRegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult);
        if (status == ERROR_SUCCESS && phkResult && *phkResult) remember_reg_path(*phkResult, path);
    } else {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    }
    log_reg_open_w("RegOpenKeyExA", path, status, phkResult ? *phkResult : NULL);
    return status;
}

static LONG WINAPI shim_RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved,
                                         LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    LONG status = ERROR_CALL_NOT_IMPLEMENTED;
    DWORD type = lpType ? *lpType : 0;
    DWORD cb = lpcbData ? *lpcbData : 0;
    ensure_init();
    if (pRegQueryValueExW) {
        status = pRegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
        type = lpType ? *lpType : type;
        cb = lpcbData ? *lpcbData : cb;
    } else {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    }
    log_reg_query_w("RegQueryValueExW", hKey, lpValueName, status, type, lpData, cb);
    return status;
}

static LONG WINAPI shim_RegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
                                         LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    LONG status = ERROR_CALL_NOT_IMPLEMENTED;
    WCHAR value_w[160];
    DWORD type = lpType ? *lpType : 0;
    DWORD cb = lpcbData ? *lpcbData : 0;
    ensure_init();
    ansi_to_wide(lpValueName ? lpValueName : "(default)", value_w, 160);
    if (pRegQueryValueExA) {
        status = pRegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
        type = lpType ? *lpType : type;
        cb = lpcbData ? *lpcbData : cb;
    } else {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    }
    log_reg_query_w("RegQueryValueExA", hKey, value_w, status, type, lpData, cb);
    return status;
}

static HMODULE WINAPI shim_LoadLibraryW(LPCWSTR lpLibFileName)
{
    HMODULE h;
    DWORD err;
    ensure_init();
    if (pLoadLibraryW) {
        h = pLoadLibraryW(lpLibFileName);
        err = GetLastError();
        log_load_w("LoadLibraryW", lpLibFileName, h, err);
        if (h && env_flag_enabled_w(L"AT_PATCH_AFTER_LOAD")) patch_known_atok_modules();
        SetLastError(err);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HMODULE WINAPI shim_LoadLibraryA(LPCSTR lpLibFileName)
{
    HMODULE h;
    DWORD err;
    ensure_init();
    if (pLoadLibraryA) {
        h = pLoadLibraryA(lpLibFileName);
        err = GetLastError();
        log_load_a("LoadLibraryA", lpLibFileName, h, err);
        if (h && env_flag_enabled_w(L"AT_PATCH_AFTER_LOAD")) patch_known_atok_modules();
        SetLastError(err);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HMODULE WINAPI shim_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE h;
    DWORD err;
    ensure_init();
    if (pLoadLibraryExW) {
        h = pLoadLibraryExW(lpLibFileName, hFile, dwFlags);
        err = GetLastError();
        log_load_w("LoadLibraryExW", lpLibFileName, h, err);
        if (h && env_flag_enabled_w(L"AT_PATCH_AFTER_LOAD")) patch_known_atok_modules();
        SetLastError(err);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HMODULE WINAPI shim_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE h;
    DWORD err;
    ensure_init();
    if (pLoadLibraryExA) {
        h = pLoadLibraryExA(lpLibFileName, hFile, dwFlags);
        err = GetLastError();
        log_load_a("LoadLibraryExA", lpLibFileName, h, err);
        if (h && env_flag_enabled_w(L"AT_PATCH_AFTER_LOAD")) patch_known_atok_modules();
        SetLastError(err);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCWSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    lstrcpynW(orig, lpName ? lpName : L"(null)", 256);
    if (rewrite_ns_name(lpName, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        lpName = rewritten;
    }
    if (pCreateMutexW) {
        h = pCreateMutexW(lpMutexAttributes, bInitialOwner, lpName);
        if (env_flag_enabled_w(L"AT_AUTOCREATE_IB") &&
            wcs_contains_i(orig, L"JsMmf_ATOK31IB_EXEC_DATA_") &&
            pCreateFileMappingW) {
            if (!g_autocreated_ib_map)
                g_autocreated_ib_map = pCreateFileMappingW(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, 0x10000, lpName);
            log_obj_w("AutoCreateIB", orig, g_autocreated_ib_map);
        }
        log_obj_w("CreateMutexW", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    if (!lpName || !MultiByteToWideChar(CP_ACP, 0, lpName, -1, orig, (int)(sizeof(orig) / sizeof(orig[0]))))
        lstrcpynW(orig, L"(null)", 256);
    if (lpName && MultiByteToWideChar(CP_ACP, 0, lpName, -1, rewritten, (int)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        if (rewrite_ns_name(rewritten, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
            if (pCreateMutexW) {
                h = pCreateMutexW(lpMutexAttributes, bInitialOwner, rewritten);
                log_obj_w("CreateMutexA", orig, h);
                return h;
            }
        }
    }
    if (pCreateMutexA) {
        h = pCreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
        log_obj_w("CreateMutexA", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_OpenMutexW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    lstrcpynW(orig, lpName ? lpName : L"(null)", 256);
    if (rewrite_ns_name(lpName, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        lpName = rewritten;
    }
    if (pOpenMutexW) {
        h = pOpenMutexW(dwDesiredAccess, bInheritHandle, lpName);
        log_obj_w("OpenMutexW", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_OpenMutexA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    if (!lpName || !MultiByteToWideChar(CP_ACP, 0, lpName, -1, orig, (int)(sizeof(orig) / sizeof(orig[0]))))
        lstrcpynW(orig, L"(null)", 256);
    if (lpName && MultiByteToWideChar(CP_ACP, 0, lpName, -1, rewritten, (int)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        if (rewrite_ns_name(rewritten, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
            if (pOpenMutexW) {
                h = pOpenMutexW(dwDesiredAccess, bInheritHandle, rewritten);
                log_obj_w("OpenMutexA", orig, h);
                return h;
            }
        }
    }
    if (pOpenMutexA) {
        h = pOpenMutexA(dwDesiredAccess, bInheritHandle, lpName);
        log_obj_w("OpenMutexA", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState,
                                       LPCWSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    lstrcpynW(orig, lpName ? lpName : L"(null)", 256);
    if (rewrite_ns_name(lpName, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        lpName = rewritten;
    }
    if (pCreateEventW) {
        h = pCreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName);
        log_obj_w("CreateEventW", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset, BOOL bInitialState,
                                       LPCSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    if (!lpName || !MultiByteToWideChar(CP_ACP, 0, lpName, -1, orig, (int)(sizeof(orig) / sizeof(orig[0]))))
        lstrcpynW(orig, L"(null)", 256);
    if (lpName && MultiByteToWideChar(CP_ACP, 0, lpName, -1, rewritten, (int)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        if (rewrite_ns_name(rewritten, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
            if (pCreateEventW) {
                h = pCreateEventW(lpEventAttributes, bManualReset, bInitialState, rewritten);
                log_obj_w("CreateEventA", orig, h);
                return h;
            }
        }
    }
    if (pCreateEventA) {
        h = pCreateEventA(lpEventAttributes, bManualReset, bInitialState, lpName);
        log_obj_w("CreateEventA", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_OpenEventW(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    lstrcpynW(orig, lpName ? lpName : L"(null)", 256);
    if (rewrite_ns_name(lpName, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        lpName = rewritten;
    }
    if (pOpenEventW) {
        h = pOpenEventW(dwDesiredAccess, bInheritHandle, lpName);
        log_obj_w("OpenEventW", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static HANDLE WINAPI shim_OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName)
{
    WCHAR rewritten[256];
    WCHAR orig[256];
    HANDLE h;
    ensure_init();
    if (!lpName || !MultiByteToWideChar(CP_ACP, 0, lpName, -1, orig, (int)(sizeof(orig) / sizeof(orig[0]))))
        lstrcpynW(orig, L"(null)", 256);
    if (lpName && MultiByteToWideChar(CP_ACP, 0, lpName, -1, rewritten, (int)(sizeof(rewritten) / sizeof(rewritten[0])))) {
        if (rewrite_ns_name(rewritten, rewritten, (DWORD)(sizeof(rewritten) / sizeof(rewritten[0])))) {
            if (pOpenEventW) {
                h = pOpenEventW(dwDesiredAccess, bInheritHandle, rewritten);
                log_obj_w("OpenEventA", orig, h);
                return h;
            }
        }
    }
    if (pOpenEventA) {
        h = pOpenEventA(dwDesiredAccess, bInheritHandle, lpName);
        log_obj_w("OpenEventA", orig, h);
        return h;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return NULL;
}

static DWORD WINAPI shim_SetNamedSecurityInfoW(LPWSTR pObjectName, DWORD ObjectType,
                                               SECURITY_INFORMATION SecurityInfo, PSID psidOwner,
                                               PSID psidGroup, PACL pDacl, PACL pSacl)
{
    (void)pObjectName;
    (void)ObjectType;
    (void)SecurityInfo;
    (void)psidOwner;
    (void)psidGroup;
    (void)pDacl;
    (void)pSacl;
    ensure_init();
    log_line2("AtNsShim: SetNamedSecurityInfoW", "");
    return ERROR_SUCCESS;
}

static DWORD WINAPI shim_GetNamedSecurityInfoW(LPWSTR pObjectName, DWORD ObjectType,
                                               SECURITY_INFORMATION SecurityInfo, PSID *ppsidOwner,
                                               PSID *ppsidGroup, PACL *ppDacl, PACL *ppSacl,
                                               PSECURITY_DESCRIPTOR *ppSecurityDescriptor)
{
    (void)pObjectName;
    (void)ObjectType;
    (void)SecurityInfo;
    ensure_init();
    log_line2("AtNsShim: GetNamedSecurityInfoW", "");
    if (ppsidOwner) *ppsidOwner = 0;
    if (ppsidGroup) *ppsidGroup = 0;
    if (ppDacl) *ppDacl = 0;
    if (ppSacl) *ppSacl = 0;
    if (ppSecurityDescriptor) *ppSecurityDescriptor = 0;
    return ERROR_SUCCESS;
}

static NTSTATUS_SHIM WINAPI shim_NtCreateSection(PHANDLE SectionHandle, ACCESS_MASK DesiredAccess,
                                                 PVOID ObjectAttributes, PLARGE_INTEGER MaximumSize,
                                                 ULONG SectionPageProtection, ULONG AllocationAttributes,
                                                 HANDLE FileHandle)
{
    NTSTATUS_SHIM status;
    ensure_init();
    if (pNtCreateSection) {
        status = pNtCreateSection(SectionHandle, DesiredAccess, ObjectAttributes, MaximumSize,
                                  SectionPageProtection, AllocationAttributes, FileHandle);
        log_nt_obj("NtCreateSection", ObjectAttributes, status, SectionHandle ? *SectionHandle : 0);
        return status;
    }
    return (NTSTATUS_SHIM)0xC0000002;
}

static NTSTATUS_SHIM WINAPI shim_NtOpenSection(PHANDLE SectionHandle, ACCESS_MASK DesiredAccess,
                                               PVOID ObjectAttributes)
{
    NTSTATUS_SHIM status;
    ensure_init();
    if (pNtOpenSection) {
        status = pNtOpenSection(SectionHandle, DesiredAccess, ObjectAttributes);
        log_nt_obj("NtOpenSection", ObjectAttributes, status, SectionHandle ? *SectionHandle : 0);
        return status;
    }
    return (NTSTATUS_SHIM)0xC0000002;
}

static NTSTATUS_SHIM WINAPI shim_NtCreateMutant(PHANDLE MutantHandle, ACCESS_MASK DesiredAccess,
                                                PVOID ObjectAttributes, BOOLEAN InitialOwner)
{
    NTSTATUS_SHIM status;
    ensure_init();
    if (pNtCreateMutant) {
        status = pNtCreateMutant(MutantHandle, DesiredAccess, ObjectAttributes, InitialOwner);
        log_nt_obj("NtCreateMutant", ObjectAttributes, status, MutantHandle ? *MutantHandle : 0);
        return status;
    }
    return (NTSTATUS_SHIM)0xC0000002;
}

static NTSTATUS_SHIM WINAPI shim_NtOpenMutant(PHANDLE MutantHandle, ACCESS_MASK DesiredAccess,
                                              PVOID ObjectAttributes)
{
    NTSTATUS_SHIM status;
    ensure_init();
    if (pNtOpenMutant) {
        status = pNtOpenMutant(MutantHandle, DesiredAccess, ObjectAttributes);
        log_nt_obj("NtOpenMutant", ObjectAttributes, status, MutantHandle ? *MutantHandle : 0);
        return status;
    }
    return (NTSTATUS_SHIM)0xC0000002;
}

/* Intercept ATOK's RaiseException to decode the MSVC C++ exception type name
 * (code 0xE06D7363). ATOK throws+catches C++ exceptions during TIP Activate;
 * the type name tells us WHAT it is failing on. x86 throw layout: args[2] ->
 * ThrowInfo; +12 -> CatchableTypeArray; [0]=count,[1]=CatchableType; +4 ->
 * TypeDescriptor; +8 -> mangled name (".?AV<class>@@"). */
static void WINAPI shim_RaiseException(DWORD code, DWORD flags, DWORD nargs, const ULONG_PTR *args)
{
    (void)flags;
    log_line("AtNsShim: RaiseException code=");
    log_hex32(code);
    log_line(" nargs=");
    log_dword_dec(nargs);
    log_line("\r\n");
    log_address_site("RaiseException ret0", __builtin_return_address(0));
    log_address_site("RaiseException ret1", __builtin_return_address(1));
    log_address_site("RaiseException ret2", __builtin_return_address(2));
    log_address_site("RaiseException ret3", __builtin_return_address(3));
    if (code == 0xE06D7363 && nargs >= 3 && args) {
        log_line("AtNsShim: C++ throw object=");
        log_hex32((DWORD)args[1]);
        log_line(" throwinfo=");
        log_hex32((DWORD)args[2]);
        log_line("\r\n");
        log_ascii_probe("AtNsShim: C++ throw object ascii=", (const unsigned char *)(ULONG_PTR)args[1], 160);
        log_exception_object_slots((const DWORD *)(ULONG_PTR)args[1]);
        const DWORD *ti = (const DWORD *)(args[2]);
        if (ti) {
            const DWORD *cta = (const DWORD *)(ULONG_PTR)ti[3];
            if (cta && cta[0] > 0 && cta[0] < 64) {
                const DWORD *ct = (const DWORD *)(ULONG_PTR)cta[1];
                if (ct) {
                    const DWORD *td = (const DWORD *)(ULONG_PTR)ct[1];
                    if (td) {
                        const char *name = (const char *)((const char *)td + 8);
                        log_line("AtNsShim: C++ throw type=");
                        log_line(name);
                        log_line("\r\n");
                    }
                }
            }
        }
    }
    if (pRaiseException) pRaiseException(code, flags, nargs, args);
}

static int patch_one_module(HMODULE mod, const WCHAR *module_name)
{
    BYTE *base;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    IMAGE_THUNK_DATA32 *orig_thunk;
    IMAGE_THUNK_DATA32 *iat;
    int patched_count = 0;
    int trace_loadlib = env_flag_enabled_w(L"AT_TRACE_LOADLIB");
    int trace_reg = env_flag_enabled_w(L"AT_TRACE_REG");

    if (!mod || !module_name) return 0;
    if (wcs_starts_with_i(module_name, L"AtNsShim.dll")) return 0;
    if (remember_patch_scan(mod)) {
        char nm[128];
        int i = 0;
        while (module_name[i] && i < (int)sizeof(nm) - 1) {
            WCHAR ch = module_name[i];
            nm[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
            i++;
        }
        nm[i] = 0;
        log_line("AtNsShim: scan ");
        log_line(nm);
        log_line("\r\n");
    }

    base = (BYTE *)mod;
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    if (nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) return 0;

    imp = (IMAGE_IMPORT_DESCRIPTOR *)(base +
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    for (; imp->Name; imp++) {
        const char *dll = (const char *)(base + imp->Name);
        if (!dll) continue;
        if (lstrcmpiA(dll, "KERNEL32.dll") != 0 &&
            lstrcmpiA(dll, "KERNELBASE.dll") != 0 &&
            lstrcmpiA(dll, "ADVAPI32.dll") != 0 &&
            lstrcmpiA(dll, "ntdll.dll") != 0) {
            continue;
        }
        orig_thunk = (IMAGE_THUNK_DATA32 *)(base + imp->OriginalFirstThunk);
        iat = (IMAGE_THUNK_DATA32 *)(base + imp->FirstThunk);
        for (; orig_thunk && orig_thunk->u1.AddressOfData; orig_thunk++, iat++) {
            if (orig_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
            {
                IMAGE_IMPORT_BY_NAME *name = (IMAGE_IMPORT_BY_NAME *)(base + orig_thunk->u1.AddressOfData);
                void **slot = (void **)&iat->u1.Function;
                void *replacement = 0;
                if (!name || !name->Name[0]) continue;
                if (lstrcmpiA((const char *)name->Name, "CreateBoundaryDescriptorW") == 0) replacement = shim_CreateBoundaryDescriptorW;
                else if (lstrcmpiA((const char *)name->Name, "AddSIDToBoundaryDescriptor") == 0) replacement = shim_AddSIDToBoundaryDescriptor;
                else if (lstrcmpiA((const char *)name->Name, "DeleteBoundaryDescriptor") == 0) replacement = shim_DeleteBoundaryDescriptor;
                else if (lstrcmpiA((const char *)name->Name, "CreatePrivateNamespaceW") == 0) replacement = shim_CreatePrivateNamespaceW;
                else if (lstrcmpiA((const char *)name->Name, "OpenPrivateNamespaceW") == 0) replacement = shim_OpenPrivateNamespaceW;
                else if (lstrcmpiA((const char *)name->Name, "ClosePrivateNamespace") == 0) replacement = shim_ClosePrivateNamespace;
                else if (lstrcmpiA((const char *)name->Name, "CreateFileMappingW") == 0) replacement = shim_CreateFileMappingW;
                else if (lstrcmpiA((const char *)name->Name, "OpenFileMappingW") == 0) replacement = shim_OpenFileMappingW;
                else if (lstrcmpiA((const char *)name->Name, "CreateFileMappingA") == 0) replacement = shim_CreateFileMappingA;
                else if (lstrcmpiA((const char *)name->Name, "OpenFileMappingA") == 0) replacement = shim_OpenFileMappingA;
                else if (lstrcmpiA((const char *)name->Name, "MapViewOfFile") == 0) replacement = shim_MapViewOfFile;
                else if (lstrcmpiA((const char *)name->Name, "CreateFileW") == 0) replacement = shim_CreateFileW;
                else if (lstrcmpiA((const char *)name->Name, "CreateFileA") == 0) replacement = shim_CreateFileA;
                else if (lstrcmpiA((const char *)name->Name, "GetFileAttributesW") == 0) replacement = shim_GetFileAttributesW;
                else if (lstrcmpiA((const char *)name->Name, "GetFileAttributesA") == 0) replacement = shim_GetFileAttributesA;
                else if (lstrcmpiA((const char *)name->Name, "FindFirstFileW") == 0) replacement = shim_FindFirstFileW;
                else if (lstrcmpiA((const char *)name->Name, "FindFirstFileA") == 0) replacement = shim_FindFirstFileA;
                else if (trace_loadlib && lstrcmpiA((const char *)name->Name, "LoadLibraryW") == 0) replacement = shim_LoadLibraryW;
                else if (trace_loadlib && lstrcmpiA((const char *)name->Name, "LoadLibraryA") == 0) replacement = shim_LoadLibraryA;
                else if (trace_loadlib && lstrcmpiA((const char *)name->Name, "LoadLibraryExW") == 0) replacement = shim_LoadLibraryExW;
                else if (trace_loadlib && lstrcmpiA((const char *)name->Name, "LoadLibraryExA") == 0) replacement = shim_LoadLibraryExA;
                else if (trace_reg && lstrcmpiA((const char *)name->Name, "RegOpenKeyW") == 0) replacement = shim_RegOpenKeyW;
                else if (trace_reg && lstrcmpiA((const char *)name->Name, "RegOpenKeyExW") == 0) replacement = shim_RegOpenKeyExW;
                else if (trace_reg && lstrcmpiA((const char *)name->Name, "RegOpenKeyExA") == 0) replacement = shim_RegOpenKeyExA;
                else if (trace_reg && lstrcmpiA((const char *)name->Name, "RegQueryValueExW") == 0) replacement = shim_RegQueryValueExW;
                else if (trace_reg && lstrcmpiA((const char *)name->Name, "RegQueryValueExA") == 0) replacement = shim_RegQueryValueExA;
                else if (lstrcmpiA((const char *)name->Name, "CreateMutexW") == 0) replacement = shim_CreateMutexW;
                else if (lstrcmpiA((const char *)name->Name, "CreateMutexA") == 0) replacement = shim_CreateMutexA;
                else if (lstrcmpiA((const char *)name->Name, "OpenMutexW") == 0) replacement = shim_OpenMutexW;
                else if (lstrcmpiA((const char *)name->Name, "OpenMutexA") == 0) replacement = shim_OpenMutexA;
                else if (lstrcmpiA((const char *)name->Name, "CreateEventW") == 0) replacement = shim_CreateEventW;
                else if (lstrcmpiA((const char *)name->Name, "CreateEventA") == 0) replacement = shim_CreateEventA;
                else if (lstrcmpiA((const char *)name->Name, "OpenEventW") == 0) replacement = shim_OpenEventW;
                else if (lstrcmpiA((const char *)name->Name, "OpenEventA") == 0) replacement = shim_OpenEventA;
                else if (lstrcmpiA((const char *)name->Name, "SetNamedSecurityInfoW") == 0) replacement = shim_SetNamedSecurityInfoW;
                else if (lstrcmpiA((const char *)name->Name, "GetNamedSecurityInfoW") == 0) replacement = shim_GetNamedSecurityInfoW;
                else if (lstrcmpiA((const char *)name->Name, "NtCreateSection") == 0) replacement = shim_NtCreateSection;
                else if (lstrcmpiA((const char *)name->Name, "NtOpenSection") == 0) replacement = shim_NtOpenSection;
                else if (lstrcmpiA((const char *)name->Name, "NtCreateMutant") == 0) replacement = shim_NtCreateMutant;
                else if (lstrcmpiA((const char *)name->Name, "NtOpenMutant") == 0) replacement = shim_NtOpenMutant;
                else if (lstrcmpiA((const char *)name->Name, "RaiseException") == 0) replacement = shim_RaiseException;
                if (replacement && *slot != replacement) {
                    DWORD oldprot;
                    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldprot)) {
                        *slot = replacement;
                        VirtualProtect(slot, sizeof(void *), oldprot, &oldprot);
                        patched_count++;
                    }
                }
            }
        }
    }
    if (patched_count > 0) {
        char nm[128];
        int i = 0;
        while (module_name[i] && i < (int)sizeof(nm) - 1) {
            WCHAR ch = module_name[i];
            nm[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
            i++;
        }
        nm[i] = 0;
        log_line("AtNsShim: patched ");
        log_dword_dec((DWORD)patched_count);
        log_line(" imports in ");
        log_line(nm);
        log_line("\r\n");
    }
    return patched_count;
}

/* True if module base name looks like an ATOK module we should patch. */
static int is_atok_module_name(const WCHAR *name)
{
    return wcs_starts_with_i(name, L"ATOK") ||
           wcs_starts_with_i(name, L"Atok") ||
           wcs_starts_with_i(name, L"ATFSVR") ||
           wcs_starts_with_i(name, L"JSFLT");
}

static const WCHAR *base_name_w(const WCHAR *path)
{
    const WCHAR *p = path;
    const WCHAR *base = path;
    for (; *p; p++) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    return base;
}

/* Patch the main exe (if it's an ATOK process) plus every loaded ATOK* module,
 * so namespace/shared-memory calls from ATOK31OM/ATOK31MN/ATOKLIB/etc. get the
 * same name-rewriting as the TIP and therefore share the same JsMmf objects. */
static void patch_known_atok_modules(void)
{
    HMODULE main_mod = GetModuleHandleW(NULL);
    WCHAR exe_path[MAX_PATH];
    HANDLE snap;
    MODULEENTRY32W me;

    if (main_mod && GetModuleFileNameW(main_mod, exe_path, MAX_PATH)) {
        if (is_atok_module_name(base_name_w(exe_path))) {
            patch_one_module(main_mod, base_name_w(exe_path));
        }
    }

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        me.dwSize = sizeof(me);
        if (Module32FirstW(snap, &me)) {
            do {
                if (is_atok_module_name(base_name_w(me.szModule))) {
                    patch_one_module(me.hModule, me.szModule);
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
    } else {
        /* Fallback to the original explicit list if toolhelp is unavailable. */
        HMODULE tip = GetModuleHandleW(L"ATOK31TIP.DLL");
        HMODULE rt = GetModuleHandleW(L"ATOK31RT.DLL");
        if (tip) patch_one_module(tip, L"ATOK31TIP.DLL");
        if (rt) patch_one_module(rt, L"ATOK31RT.DLL");
    }
}

static int host_is_atok_process(void)
{
    WCHAR path[MAX_PATH];
    const WCHAR *base;
    if (!GetModuleFileNameW(GetModuleHandleW(NULL), path, MAX_PATH)) return 0;
    base = base_name_w(path);
    if (wcs_starts_with_i(base, L"AtLaunch")) return 0;
    return is_atok_module_name(base);
}

static DWORD WINAPI worker_thread(LPVOID unused)
{
    (void)unused;
    log_line("AtNsShim: worker started\r\n");
    ensure_init();
    for (;;) {
        patch_known_atok_modules();
        Sleep(50);
    }
    return 0;
}

static int host_is_atok_tipload(void)
{
    WCHAR path[MAX_PATH];
    if (!GetModuleFileNameW(GetModuleHandleW(NULL), path, MAX_PATH)) return 0;
    return wcs_starts_with_i(base_name_w(path), L"AtTipLoad") ||
           wcs_starts_with_i(base_name_w(path), L"atoktipload");
}

__declspec(dllexport) void WINAPI AtNsShimInstall(void)
{
    WCHAR worker_buf[8];

    log_line("AtNsShim: install\r\n");
    ensure_init();
    if (host_is_atok_tipload())
        precreate_dn_maps_if_requested();
    patch_known_atok_modules();
    /* Avoid racing IAT patches against ATOK31TIP.DLL DllMain in the TIP host. */
    if (host_is_atok_tipload())
        return;
    if (GetEnvironmentVariableW(L"AT_NSSHIM_WORKER", worker_buf,
                                (DWORD)(sizeof(worker_buf) / sizeof(worker_buf[0]))) > 0 &&
        worker_buf[0] == L'0') {
        return;
    }
    if (!g_worker_thread) {
        g_worker_thread = CreateThread(0, 0, worker_thread, 0, 0, &g_worker_tid);
        if (!g_worker_thread) {
            log_line("AtNsShim: CreateThread failed\r\n");
        }
    }
}

__declspec(dllexport) void WINAPI AtNsShimPatchNow(void)
{
    log_line("AtNsShim: patch now\r\n");
    ensure_init();
    patch_known_atok_modules();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        /* AppInit-loaded into every process; only activate for ATOK processes
         * (e.g. ATOK31OM/ATOK31MN servers) so their namespace + shared-memory
         * calls are rewritten consistently with the TIP. Patch synchronously
         * here (static imports are already loaded); the resident worker thread
         * is only started via the explicit AtNsShimInstall() the TIP calls. */
        if (host_is_atok_process()) {
            ensure_init();
            patch_known_atok_modules();
            if (!host_is_atok_tipload() && !g_worker_thread) {
                g_worker_thread = CreateThread(0, 0, worker_thread, 0, 0, &g_worker_tid);
            }
        }
    }
    return TRUE;
}
