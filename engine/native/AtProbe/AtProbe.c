typedef void *HWND;
typedef void *HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned short wchar_t;
typedef const wchar_t *LPCWSTR;
typedef void *HIMC;
typedef long long LPARAM;

#define STD_OUTPUT_HANDLE ((DWORD)-11)

__attribute__((dllimport)) HWND __stdcall FindWindowW(LPCWSTR lpClassName, LPCWSTR lpWindowName);
__attribute__((dllimport)) HWND __stdcall FindWindowExW(HWND hWndParent, HWND hWndChildAfter, LPCWSTR lpszClass, LPCWSTR lpszWindow);
__attribute__((dllimport)) BOOL __stdcall EnumWindows(BOOL (__stdcall *lpEnumFunc)(HWND, LPARAM), LPARAM lParam);
__attribute__((dllimport)) int __stdcall GetClassNameW(HWND hWnd, wchar_t *lpClassName, int nMaxCount);
__attribute__((dllimport)) int __stdcall GetWindowTextW(HWND hWnd, wchar_t *lpString, int nMaxCount);
__attribute__((dllimport)) unsigned int __stdcall RegisterWindowMessageW(LPCWSTR lpString);
__attribute__((dllimport)) HANDLE __stdcall GetStdHandle(DWORD nStdHandle);
__attribute__((dllimport)) BOOL __stdcall WriteFile(HANDLE hFile, const void *buffer, DWORD nBytes, DWORD *written, void *overlapped);
__attribute__((dllimport)) void __stdcall Sleep(DWORD ms);
__attribute__((dllimport)) void __stdcall ExitProcess(unsigned int code);
__attribute__((dllimport)) HIMC __stdcall ImmGetContext(HWND hWnd);
__attribute__((dllimport)) BOOL __stdcall ImmReleaseContext(HWND hWnd, HIMC hIMC);
__attribute__((dllimport)) BOOL __stdcall ImmGetOpenStatus(HIMC hIMC);
__attribute__((dllimport)) BOOL __stdcall ImmGetConversionStatus(HIMC hIMC, DWORD *lpfdwConversion, DWORD *lpfdwSentence);

static int c_strlen(const char *s)
{
    int n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void *memcpy(void *dst, const void *src, unsigned long long n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n-- != 0) {
        *d++ = *s++;
    }
    return dst;
}

static void narrow_wide(char *dst, int cap, const wchar_t *src)
{
    int i = 0;
    if (cap <= 0) {
        return;
    }
    while (src[i] != 0 && i < cap - 1) {
        wchar_t ch = src[i];
        dst[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
        i++;
    }
    dst[i] = '\0';
}

static int contains_icase(const char *haystack, const char *needle)
{
    int i;
    int j;

    if (!*needle) {
        return 1;
    }

    for (i = 0; haystack[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0'; j++) {
            char a = haystack[i + j];
            char b = needle[j];

            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
        }
        if (needle[j] == '\0') {
            return 1;
        }
    }

    return 0;
}

static void write_all(const char *s)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    WriteFile(out, s, (DWORD)c_strlen(s), &written, 0);
}

static char *append_str(char *dst, const char *src)
{
    while (*src != '\0') {
        *dst++ = *src++;
    }
    return dst;
}

static char *append_hex_u64(char *dst, unsigned long long value, int width)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = (width - 1); i >= 0; i--) {
        dst[i] = hex[value & 0xF];
        value >>= 4;
    }
    return dst + width;
}

static void write_line(const char *s)
{
    write_all(s);
    write_all("\r\n");
}

static void format_window(HWND hwnd, char *buf, int cap)
{
    wchar_t class_w[256];
    wchar_t title_w[256];
    char class_a[256];
    char title_a[256];
    char *p = buf;

    if (cap <= 0) {
        return;
    }

    class_w[0] = 0;
    title_w[0] = 0;
    class_a[0] = 0;
    title_a[0] = 0;

    GetClassNameW(hwnd, class_w, 256);
    GetWindowTextW(hwnd, title_w, 256);
    narrow_wide(class_a, 256, class_w);
    narrow_wide(title_a, 256, title_w);

    p = append_str(p, "hwnd=0x");
    p = append_hex_u64(p, (unsigned long long)(unsigned long long)(unsigned long long)(unsigned long long)hwnd, 16);
    p = append_str(p, " class=\"");
    p = append_str(p, class_a);
    p = append_str(p, "\" title=\"");
    p = append_str(p, title_a);
    p = append_str(p, "\"");
    *p = '\0';
}

static void probe_window(void)
{
    struct candidate {
        int by_class;
        const wchar_t *class_name;
        const wchar_t *title;
        const char *label;
    } candidates[] = {
        { 1, L"ATOK31TIP UI Window Class Name", 0, "ATOK31TIP UI Window Class Name" },
        { 1, L"ATOK31TRAYMANAGER", 0, "ATOK31TRAYMANAGER" },
        { 0, 0, L"ATOK31TIP UI Window Name", "ATOK31TIP UI Window Name" },
    };

    int i, j;
    for (i = 0; i < 40; i++) {
        for (j = 0; j < (int)(sizeof(candidates) / sizeof(candidates[0])); j++) {
            HWND hwnd = candidates[j].by_class
                ? FindWindowW(candidates[j].class_name, 0)
                : FindWindowW(0, candidates[j].title);
            if (hwnd != 0) {
                char buf[512];
                write_all("found ");
                write_all(candidates[j].label);
                write_all(" => ");
                format_window(hwnd, buf, 512);
                write_line(buf);
                return;
            }
        }
        Sleep(250);
    }

    write_line("found <none>");
}

static void probe_messages(void)
{
    static const wchar_t *messages[] = {
        L"Atok Message for ReconvertString",
        L"Atok Message for DocumentFeed",
        L"Atok Message for Property Change Report",
        L"Atok Message for ExtContext Change",
        L"ATOK ExtraTrans Message for Composition Report",
        L"ATOK ExtraTrans Message for Trans Result",
        L"ATOK ExtraTrans Message for Optional Function Execute",
        L"AT_PROPERTY_APPLY_EVENT_MESSAGE",
        L"AT_API_MESSAGE_FOR_CLEAR_LEARN_DATA",
        L"DRT_MESSAGE",
        L"IncrementTip",
    };
    static const char *labels[] = {
        "Atok Message for ReconvertString",
        "Atok Message for DocumentFeed",
        "Atok Message for Property Change Report",
        "Atok Message for ExtContext Change",
        "ATOK ExtraTrans Message for Composition Report",
        "ATOK ExtraTrans Message for Trans Result",
        "ATOK ExtraTrans Message for Optional Function Execute",
        "AT_PROPERTY_APPLY_EVENT_MESSAGE",
        "AT_API_MESSAGE_FOR_CLEAR_LEARN_DATA",
        "DRT_MESSAGE",
        "IncrementTip",
    };
    unsigned int i;

    for (i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
        unsigned int id = RegisterWindowMessageW(messages[i]);
        char line[256];
        char *p = line;

        p = append_str(p, "msg ");
        p = append_str(p, labels[i]);
        p = append_str(p, " = 0x");
        p = append_hex_u64(p, id, 4);
        *p = '\0';
        write_line(line);
    }
}

static void dump_target_window(HWND main_hwnd)
{
    HWND edit_hwnd = FindWindowExW(main_hwnd, 0, L"EDIT", 0);
    wchar_t title_w[256];
    char title_a[256];
    char buf[512];
    char *p;
    int len;
    HIMC hImc;
    DWORD conv = 0;
    DWORD sent = 0;

    p = buf;
    p = append_str(p, "target hwnd=0x");
    p = append_hex_u64(p, (unsigned long long)main_hwnd, 16);
    p = append_str(p, " edit=0x");
    p = append_hex_u64(p, (unsigned long long)edit_hwnd, 16);
    *p = '\0';
    write_line(buf);

    if (!edit_hwnd) {
        write_line("target edit not found");
        return;
    }

    GetWindowTextW(edit_hwnd, title_w, 256);
    narrow_wide(title_a, 256, title_w);
    p = buf;
    p = append_str(p, "target edit text=\"");
    p = append_str(p, title_a);
    p = append_str(p, "\"");
    *p = '\0';
    write_line(buf);

    len = GetWindowTextW(edit_hwnd, title_w, 256);
    p = buf;
    p = append_str(p, "target edit len=");
    p = append_hex_u64(p, (unsigned long long)len, 4);
    p = append_str(p, " raw=");
    for (int i = 0; i < len && i < 256; i++) {
        p = append_str(p, "U+");
        p = append_hex_u64(p, (unsigned long long)title_w[i], 4);
        if (i + 1 < len) {
            *p++ = ' ';
        }
    }
    *p = '\0';
    write_line(buf);

    hImc = ImmGetContext(edit_hwnd);
    if (hImc) {
        p = buf;
        if (ImmGetOpenStatus(hImc)) {
            p = append_str(p, "target IMM open=1");
        } else {
            p = append_str(p, "target IMM open=0");
        }
        if (ImmGetConversionStatus(hImc, &conv, &sent)) {
            p = append_str(p, " conv=0x");
            p = append_hex_u64(p, (unsigned long long)conv, 8);
            p = append_str(p, " sent=0x");
            p = append_hex_u64(p, (unsigned long long)sent, 8);
        } else {
            p = append_str(p, " conv=<failed>");
        }
        *p = '\0';
        write_line(buf);
        ImmReleaseContext(edit_hwnd, hImc);
    } else {
        write_line("target IMM no context");
    }
}

static BOOL __stdcall enum_windows_proc(HWND hwnd, LPARAM lParam)
{
    wchar_t class_w[256];
    wchar_t title_w[256];
    char class_a[256];
    char title_a[256];
    char line[768];
    char *p;

    class_w[0] = 0;
    title_w[0] = 0;
    GetClassNameW(hwnd, class_w, 256);
    GetWindowTextW(hwnd, title_w, 256);
    narrow_wide(class_a, 256, class_w);
    narrow_wide(title_a, 256, title_w);

    if (!contains_icase(class_a, "atok") && !contains_icase(title_a, "atok") && !contains_icase(class_a, "edit") && !contains_icase(title_a, "edit")) {
        return 1;
    }

    p = line;
    p = append_str(p, "enum hwnd=0x");
    p = append_hex_u64(p, (unsigned long long)hwnd, 16);
    p = append_str(p, " class=\"");
    p = append_str(p, class_a);
    p = append_str(p, "\" title=\"");
    p = append_str(p, title_a);
    p = append_str(p, "\"");
    *p = '\0';
    write_line(line);

    if (contains_icase(class_a, "atoktargetwindow")) {
        dump_target_window(hwnd);
    }
    return 1;
}

static void probe_windows(void)
{
    EnumWindows(enum_windows_proc, 0);
}

static void probe_target_text(void)
{
    probe_windows();
}

void __stdcall mainCRTStartup(void)
{
    probe_windows();
    probe_target_text();
    probe_window();
    probe_messages();
    ExitProcess(0);
}
