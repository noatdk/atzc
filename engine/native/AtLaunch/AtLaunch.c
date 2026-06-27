#include <windows.h>

static int cstrlen(const char *s) {
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

static void mzero(void *p, DWORD n) {
  BYTE *b = (BYTE *)p;
  while (n--) {
    *b++ = 0;
  }
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

static int wstarts_with_icase(const wchar_t *s, const wchar_t *prefix) {
  while (*prefix) {
    wchar_t a = *s++;
    wchar_t b = *prefix++;
    if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
    if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
    if (a != b) return 0;
  }
  return 1;
}

static wchar_t *copy_wstr(wchar_t *dst, const wchar_t *src) {
  wchar_t *out = dst;
  while ((*dst++ = *src++) != 0) {}
  return out;
}

int mainCRTStartup(void) {
  wchar_t *cmdline = GetCommandLineW();
  wchar_t *target = first_arg(cmdline);
  wchar_t dll_path[MAX_PATH];
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  LPVOID remote_mem = NULL;
  HANDLE remote_thread = NULL;
  DWORD remote_exit = 0;
  FARPROC load_library = NULL;
  HMODULE k32 = NULL;

  if (!target || !*target) {
    write_out("atok-launch: missing target path\n");
    ExitProcess(1);
  }

  mzero(&si, sizeof(si));
  mzero(&pi, sizeof(pi));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  if (si.hStdInput && si.hStdInput != INVALID_HANDLE_VALUE)
    SetHandleInformation(si.hStdInput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  if (si.hStdOutput && si.hStdOutput != INVALID_HANDLE_VALUE)
    SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  if (si.hStdError && si.hStdError != INVALID_HANDLE_VALUE)
    SetHandleInformation(si.hStdError, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

  if (!CreateProcessW(NULL, target, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
    write_out("atok-launch: CreateProcessW failed\n");
    ExitProcess(2);
  }

  k32 = GetModuleHandleW(L"kernel32.dll");
  if (!k32) {
    write_out("atok-launch: GetModuleHandleW(kernel32) failed\n");
    TerminateProcess(pi.hProcess, 3);
    ExitProcess(3);
  }

  load_library = GetProcAddress(k32, "LoadLibraryW");
  if (!load_library) {
    write_out("atok-launch: GetProcAddress(LoadLibraryW) failed\n");
    TerminateProcess(pi.hProcess, 4);
    ExitProcess(4);
  }

  /* Inject AtNsShim.dll FIRST (it auto-installs its IAT patches for ATOK
   * processes, so ATOK31OM/ATFSVR31 rewrite their AtokPrivateNamespace\JsMmf*
   * names the same way the TIP does and therefore share the shared memory),
   * then mscoree.dll (for the .NET service stub). Both are best-effort. */
  {
    const wchar_t *inject[2];
    int di;
    inject[0] = L"C:\\windows\\system32\\AtNsShim.dll";
    inject[1] = L"C:\\windows\\system32\\mscoree.dll";
    for (di = 0; di < 2; di++) {
      DWORD nbytes;
      copy_wstr(dll_path, inject[di]);
      nbytes = (DWORD)((lstrlenW(dll_path) + 1) * sizeof(wchar_t));
      remote_mem = VirtualAllocEx(pi.hProcess, NULL, nbytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
      if (!remote_mem) { write_out("atok-launch: VirtualAllocEx failed\n"); continue; }
      if (!WriteProcessMemory(pi.hProcess, remote_mem, dll_path, nbytes, NULL)) {
        write_out("atok-launch: WriteProcessMemory failed\n"); continue;
      }
      remote_thread = CreateRemoteThread(pi.hProcess, NULL, 0,
                                         (LPTHREAD_START_ROUTINE)load_library,
                                         remote_mem, 0, NULL);
      if (!remote_thread) { write_out("atok-launch: CreateRemoteThread failed\n"); continue; }
      WaitForSingleObject(remote_thread, INFINITE);
      remote_exit = 0;
      GetExitCodeThread(remote_thread, &remote_exit);
      CloseHandle(remote_thread);
      if (di == 0) {
        write_out(remote_exit ? "atok-launch: AtNsShim injected\n"
                              : "atok-launch: AtNsShim injection failed\n");
        if (remote_exit) {
          HMODULE local_shim;
          FARPROC local_install;
          FARPROC remote_install;

          local_shim = ((HMODULE (WINAPI *)(LPCWSTR))load_library)(inject[di]);
          local_install = local_shim ? GetProcAddress(local_shim, "AtNsShimInstall") : NULL;
          if (!local_install && local_shim) {
            local_install = GetProcAddress(local_shim, "AtNsShimInstall@0");
          }
          if (local_install) {
            remote_install = (FARPROC)((BYTE *)(ULONG_PTR)remote_exit +
                            ((BYTE *)local_install - (BYTE *)local_shim));
            remote_thread = CreateRemoteThread(pi.hProcess, NULL, 0,
                                               (LPTHREAD_START_ROUTINE)remote_install,
                                               NULL, 0, NULL);
            if (remote_thread) {
              DWORD wait_rc = WaitForSingleObject(remote_thread, 3000);
              CloseHandle(remote_thread);
              write_out(wait_rc == WAIT_OBJECT_0
                        ? "atok-launch: AtNsShimInstall called\n"
                        : "atok-launch: AtNsShimInstall still running\n");
            } else {
              write_out("atok-launch: AtNsShimInstall thread failed\n");
            }
          } else {
            write_out("atok-launch: AtNsShimInstall export missing\n");
          }
        }
      }
    }
  }

  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (wstarts_with_icase(target, L"ATFSVR31.EXE")) {
    write_out("atok-launch: ATFSVR31 resumed with mscoree loaded\n");
  }

  ExitProcess(0);
}
