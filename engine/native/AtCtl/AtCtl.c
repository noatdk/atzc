#include <windows.h>

static int cstrlen(const char *s) {
  int n = 0;
  while (s && s[n]) n++;
  return n;
}

static int wstrlen(const wchar_t *s) {
  int n = 0;
  while (s && s[n]) n++;
  return n;
}

static void write_out(const char *s) {
  DWORD written = 0;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    WriteFile(h, s, (DWORD)cstrlen(s), &written, NULL);
  }
}

typedef struct SearchState {
  const wchar_t *class_name;
  const wchar_t *title;
  HWND found;
  int list_only;
  DWORD target_pid;
} SearchState;

static int wcontains_i(const wchar_t *s, const wchar_t *needle) {
  int i;
  int nlen = wstrlen(needle);
  if (!needle || !*needle) return 1;
  if (!s) return 0;
  for (; *s; s++) {
    for (i = 0; i < nlen; i++) {
      wchar_t a = s[i];
      wchar_t b = needle[i];
      if (!a) return 0;
      if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
      if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
      if (a != b) break;
    }
    if (i == nlen) return 1;
  }
  return 0;
}

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp) {
  SearchState *st = (SearchState *)(ULONG_PTR)lp;
  wchar_t class_buf[128];
  wchar_t title_buf[256];
  char line[512];
  DWORD pid = 0;
  class_buf[0] = 0;
  title_buf[0] = 0;
  GetClassNameW(hwnd, class_buf, (int)(sizeof(class_buf) / sizeof(class_buf[0])));
  GetWindowTextW(hwnd, title_buf, (int)(sizeof(title_buf) / sizeof(title_buf[0])));
  GetWindowThreadProcessId(hwnd, &pid);
  if (st->list_only) {
    int i = 0, j = 0;
    for (i = 0; i < (int)sizeof(line) - 1 && class_buf[i]; i++) {
      wchar_t ch = class_buf[i];
      line[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
    }
    line[i++] = ' ';
    line[i++] = '|';
    line[i++] = ' ';
    for (j = 0; i < (int)sizeof(line) - 1 && title_buf[j]; j++, i++) {
      wchar_t ch = title_buf[j];
      line[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
    }
    line[i] = 0;
    {
      char pid_buf[64];
      int j = 0;
      wsprintfA(pid_buf, " [pid=%lu]", (unsigned long)pid);
      if (i + cstrlen(pid_buf) < (int)sizeof(line) - 1) {
        for (j = 0; pid_buf[j] && i < (int)sizeof(line) - 1; j++, i++) {
          line[i] = pid_buf[j];
        }
        line[i] = 0;
      }
    }
    write_out(line);
    write_out("\n");
  }
  if (st->target_pid && pid != st->target_pid) {
    return TRUE;
  }
  if (st->class_name && wcontains_i(class_buf, st->class_name)) {
    st->found = hwnd;
    return FALSE;
  }
  if (st->title && wcontains_i(title_buf, st->title)) {
    st->found = hwnd;
    return FALSE;
  }
  return TRUE;
}

static wchar_t *skip_ws(wchar_t *p) {
  while (*p == L' ' || *p == L'\t') p++;
  return p;
}

static wchar_t *first_arg(wchar_t *cmdline) {
  wchar_t *p = skip_ws(cmdline);
  if (*p == L'"') {
    p++;
    while (*p && *p != L'"') p++;
    if (*p == L'"') p++;
  } else {
    while (*p && *p != L' ' && *p != L'\t') p++;
  }
  return skip_ws(p);
}

static wchar_t *skip_one_arg(wchar_t *p) {
  p = skip_ws(p);
  if (*p == L'"') {
    p++;
    while (*p && *p != L'"') p++;
    if (*p == L'"') p++;
  } else {
    while (*p && *p != L' ' && *p != L'\t') p++;
  }
  return skip_ws(p);
}

static int token_equals(const wchar_t *s, const wchar_t *token) {
  while (*token && *s && *s != L' ' && *s != L'\t') {
    wchar_t a = *s++;
    wchar_t b = *token++;
    if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
    if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
    if (a != b) return 0;
  }
  return *token == 0 && (*s == 0 || *s == L' ' || *s == L'\t');
}

static DWORD parse_pid_token(const wchar_t *s) {
  DWORD pid = 0;
  int seen = 0;
  while (*s == L' ' || *s == L'\t') s++;
  while (*s >= L'0' && *s <= L'9') {
    pid = pid * 10 + (DWORD)(*s - L'0');
    s++;
    seen = 1;
  }
  return seen ? pid : 0;
}

static HWND find_target_window(void) {
  SearchState st;
  st.class_name = L"AtTargetWindow";
  st.title = L"ATOK target";
  st.found = 0;
  st.list_only = 0;
  EnumWindows(enum_windows_cb, (LPARAM)&st);
  if (!st.found) {
    st.found = FindWindowW(L"AtTargetWindow", 0);
  }
  return st.found;
}

static HWND find_tip_window(void) {
  SearchState st;
  st.class_name = L"AtTipHostFrame";
  st.title = L"AtTipHost";
  st.found = 0;
  st.list_only = 0;
  EnumWindows(enum_windows_cb, (LPARAM)&st);
  if (!st.found) {
    st.found = FindWindowW(L"AtTipHostFrame", 0);
  }
  if (!st.found) {
    st.found = FindWindowW(0, L"AtTipHost (focus here: type romaji)");
  }
  return st.found;
}

void __stdcall mainCRTStartup(void) {
  wchar_t *cmdline = GetCommandLineW();
  wchar_t *arg = first_arg(cmdline);
  wchar_t *cmd = arg;
  wchar_t target_class[64];
  char payload[512];
  int i = 0;
  DWORD target_pid = 0;
  HWND hwnd;
  COPYDATASTRUCT cds;
  LRESULT ret;

  if (!arg || !*arg) {
    write_out("usage: AtCtl.exe <command>\n");
    ExitProcess(1);
  }

  if (token_equals(arg, L"list")) {
    SearchState st;
    st.class_name = 0;
    st.title = 0;
    st.found = 0;
    st.list_only = 1;
    st.target_pid = 0;
    EnumWindows(enum_windows_cb, (LPARAM)&st);
    ExitProcess(0);
  }

  lstrcpynW(target_class, L"AtTargetWindow", (int)(sizeof(target_class) / sizeof(target_class[0])));
  if (token_equals(arg, L"tip")) {
    lstrcpynW(target_class, L"AtTipHostFrame", (int)(sizeof(target_class) / sizeof(target_class[0])));
    cmd = skip_one_arg(arg);
  } else if (token_equals(arg, L"target")) {
    cmd = skip_one_arg(arg);
  }

  target_pid = parse_pid_token(cmd);
  if (target_pid) {
    cmd = skip_one_arg(cmd);
  }

  if (!cmd || !*cmd) {
    write_out("AtCtl: missing command\n");
    ExitProcess(1);
  }
  /* A quoted command (e.g. "reset; type kisha; reconv") is the whole payload:
   * strip the opening quote and copy through to the closing quote so multi-word
   * command scripts survive. (Previously this skipped PAST the quoted content
   * and copied whatever followed it — i.e. nothing.) */
  {
    int quoted = 0;
    if (*cmd == L'"') { cmd++; quoted = 1; }
    for (i = 0; cmd[i] && i < (int)sizeof(payload) - 1; i++) {
      wchar_t ch = cmd[i];
      if (quoted && ch == L'"') break;
      payload[i] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
    }
    payload[i] = 0;
  }

  if (!lstrcmpiW(target_class, L"AtTipHostFrame")) {
    SearchState st;
    st.class_name = L"AtTipHostFrame";
    st.title = L"AtTipHost";
    st.found = 0;
    st.list_only = 0;
    st.target_pid = target_pid;
    EnumWindows(enum_windows_cb, (LPARAM)&st);
    hwnd = st.found;
    if (!hwnd && !target_pid) {
      hwnd = FindWindowW(L"AtTipHostFrame", 0);
    }
  } else {
    SearchState st;
    st.class_name = L"AtTargetWindow";
    st.title = L"ATOK target";
    st.found = 0;
    st.list_only = 0;
    st.target_pid = target_pid;
    EnumWindows(enum_windows_cb, (LPARAM)&st);
    hwnd = st.found;
    if (!hwnd && !target_pid) {
      hwnd = FindWindowW(L"AtTargetWindow", 0);
    }
  }
  if (!hwnd) {
    write_out("AtCtl: target window not found\n");
    ExitProcess(2);
  }

  cds.dwData = 0x544B544C; /* 'TKTL' */
  cds.cbData = (DWORD)(i + 1);
  cds.lpData = payload;
  ret = SendMessageW(hwnd, WM_COPYDATA, 0, (LONG_PTR)&cds);

  if (ret != 0) {
    write_out("AtCtl: command sent\n");
    ExitProcess(0);
  }

  write_out("AtCtl: target rejected command\n");
  ExitProcess(3);
}
