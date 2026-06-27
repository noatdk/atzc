#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# External deps live under one parent: $AT_HOME/{wine,at} (default .atzc/).
AT_HOME=${AT_HOME:-"$PWD/.atzc"}
LOCAL_ROOT=${AT_LOCAL_ROOT:-"$AT_HOME/at"}
HOST_ROOT=${AT_HOST_ROOT:-"$AT_HOME/at"}
INSTALL_DIR=${AT_INSTALL_DIR:-ATOK31T29}
PREFIX=${AT_WINEPREFIX:-"$PWD/.wine-atok-local"}
# Prefer the pinned Wine (fetch-wine.sh) if present; else AT_WINE; else system.
AT_WINE_PIN=""
# shellcheck source=scripts/wine-pin.sh
source "$SCRIPT_DIR/wine-pin.sh"
at_resolve_wine_pin "$AT_HOME/wine"
if [[ -n "${AT_WINE:-}" ]]; then WINE_BIN=$AT_WINE
elif [[ -n "$AT_WINE_PIN" ]]; then WINE_BIN="$AT_WINE_PIN/bin/wine"
else WINE_BIN=wine; fi
WIN_JUSTSYSTEMS="$PREFIX/drive_c/Program Files/JustSystems"
WIN_INSTALL="$WIN_JUSTSYSTEMS/${INSTALL_DIR}"
WIN_LEGACY="$WIN_JUSTSYSTEMS/ATOK"
STUB_ATOK31DE="${AT_STUB_ATOK31DE:-$PWD/native/Atok31De/Atok31De.dll}"
STUB_ATOK_NSSHIM="${AT_STUB_ATOK_NSSHIM:-$PWD/native/AtNsShim/AtNsShim.dll}"
STUB_MSCTF_SHIM="${AT_STUB_MSCTF_SHIM:-$PWD/native/MsctfShim/msctf.dll}"
WIN_SYSTEM32="$PREFIX/drive_c/windows/system32"
WIN_SYSWOW64="$PREFIX/drive_c/windows/syswow64"
# Overridable so at-provision.sh can inject the values it discovered from the
# user's own ATOK (version-agnostic); defaults match the vendored ATOK 31.
AT_TIP_CLSID=${AT_TIP_CLSID:-'{1314EB53-CACA-4152-A556-A184143202AF}'}
AT_PROFILE_GUID=${AT_PROFILE_GUID:-'{A38F2FD9-7199-45E1-841C-BE0313D8052F}'}
TSF_KEYBOARD_CATID='{34745C63-B2F0-4784-8B67-5E12C8701A31}'
LANGID_JA_HEX='0x00000411'
MSCTF_COM_CLASSES=(
  '{529A9E6B-6587-4F23-AB9E-9C7D683E3C50}'
  '{33C53A50-F456-4884-B049-85FD643ECFED}'
  '{A4B544A1-438D-4B41-9325-869523E2D6C7}'
  '{EBB08C45-6C4A-4FDC-AE53-4EB8C4C7DB8E}'
  '{3CE74DE4-53D3-4D74-8B83-431B3828BA53}'
)

usage() {
  cat <<'EOF'
usage: scripts/at-wine.sh <layout|setup|run|trace|traceipc|traceall|traceibdv|traceallcmp|traceallobj|traceallnames|tracealldelta|tracejsflt|tracejsfltraw|tracejsfltdiff|tracejsfltcmp|tmex|tmexsnap|tipsnapshot|regserver|shell> [exe-or-args...]

Commands:
  layout    Create the Wine prefix and install-tree symlinks only (no registry import).
  setup     layout + import ATOK registry (skipped when stamp is current; set AT_FORCE_REGISTRY_IMPORT=1 to redo).
  run       Launch an ATOK executable under Wine. Default: ATOK31MN.EXE.
  trace     Launch under Wine with server tracing filtered for IPC objects.
  traceipc  Trace server objects plus window/message traffic.
  traceall  Trace ATOK31MN.EXE, ATFSVR31.EXE, and JSFLT.exe sequentially.
  traceibdv Trace ATOK31IB.EXE and ATOK31DV.EXE startup around missing IPC objects.
  traceallcmp Compare the latest ATOK31MN.EXE and ATFSVR31.EXE trace markers.
  traceallobj Compare the latest ATOK31MN.EXE and ATFSVR31.EXE object markers.
  traceallnames Compare the latest ATOK31MN.EXE, ATFSVR31.EXE, and JSFLT.exe against the Windows endpoint-name set and write recon/captures/traceallnames-latest.txt.
  tracealldelta Compare the reusable traceallnames summary against the Windows IPC-name docs and write recon/captures/traceallnames-delta-latest.txt.
  tracejsflt Trace JSFLT.exe from the install tree with MSI/setupapi logging.
  tracejsfltboot Trace JSFLT.exe from a fresh prefix bootstrap (set AT_BOOTSTRAP_WINE=0 to skip pre-warming).
  tracejsfltraw Trace JSFLT.exe from a fresh prefix without pre-warming.
  tracejsfltdiff Compare the bootstrapped and raw JSFLT service-boundary markers.
  tracejsfltcmp Trace JSFLT.exe in shared and fresh prefixes and compare the startup branch.
  tmexsnap [pid] Send the `tmexsnap` runtime command through the resident TipLoad window.
  tmex [pid] Send the TMEx state-dump probe through the resident TipLoad window.
  manualtipsnap [pid] Send the `manualtipsnap` runtime command through the resident TipLoad window.
  tipsnapshot Launch AtTipLoad in one-shot snapshot mode, exit after logging the startup snapshot, and save recon/captures/tipruntime-snapshot-latest.log.
  regserver Run JSFLT.exe Regserver to register the filter backend.
  shell     Start explorer.exe on a Wine desktop; optionally launch an ATOK exe.
EOF
}

wine_env() {
  WINEPREFIX="$PREFIX" "$@"
}

run_wine() {
  wine_env "$WINE_BIN" "$@"
}

wine_build_root() {
  local loader_dir root

  [[ "$WINE_BIN" = */loader/wine ]] || return 1
  loader_dir=$(cd -- "$(dirname -- "$WINE_BIN")" && pwd)
  root=$(cd -- "$loader_dir/.." && pwd)
  [[ -d "$root/dlls" && -d "$root/programs" ]] || return 1
  printf '%s\n' "$root"
}

resolve_install_root() {
  local candidate

  for candidate in \
    "$LOCAL_ROOT/$INSTALL_DIR" \
    "$HOST_ROOT/$INSTALL_DIR"
  do
    if [[ -d "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

resolve_bundle_file() {
  local name=$1
  local candidate

  for candidate in \
    "$LOCAL_ROOT/$name" \
    "$LOCAL_ROOT/$INSTALL_DIR/$name" \
    "$LOCAL_ROOT/$INSTALL_DIR/VATOK/$name" \
    "$LOCAL_ROOT/${INSTALL_DIR}_X64/$name" \
    "$LOCAL_ROOT/${INSTALL_DIR}_X64/VATOK/$name" \
    "$HOST_ROOT/$name" \
    "$HOST_ROOT/../$name" \
    "$HOST_ROOT/$INSTALL_DIR/$name" \
    "$HOST_ROOT/$INSTALL_DIR/VATOK/$name" \
    "$HOST_ROOT/${INSTALL_DIR}_X64/$name" \
    "$HOST_ROOT/${INSTALL_DIR}_X64/VATOK/$name"
  do
    if [[ -e "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

ensure_layout() {
  local install_root wine_root pe

  install_root=$(resolve_install_root) || {
    echo "could not locate ATOK install tree under $LOCAL_ROOT or $HOST_ROOT" >&2
    exit 1
  }

  mkdir -p "$WIN_JUSTSYSTEMS"
  mkdir -p "$WIN_SYSTEM32"
  mkdir -p "$WIN_SYSWOW64"
  ln -sfn "$install_root" "$WIN_INSTALL"
  ln -sfn "$install_root" "$WIN_JUSTSYSTEMS/${INSTALL_DIR}_X64"
  ln -sfn "${LOCAL_ROOT}" "$WIN_LEGACY"
  # Atok31De.dll is the dictionary SEARCH ENGINE. Prefer the REAL 32-bit DLL from
  # SysWOW64 if present; only fall back to our loader stub (ords 5-9 -> 0, no
  # conversion) when the real engine isn't vendored.
  local atok_syswow64="$LOCAL_ROOT/SysWOW64"
  if [[ -f "$atok_syswow64/ATOK31DE.DLL" ]]; then
    ln -sfn "$atok_syswow64/ATOK31DE.DLL" "$WIN_SYSTEM32/Atok31De.dll"
    ln -sfn "$atok_syswow64/ATOK31DE.DLL" "$WIN_SYSWOW64/Atok31De.dll"
  elif [[ -f "$STUB_ATOK31DE" ]]; then
    ln -sfn "$STUB_ATOK31DE" "$WIN_SYSTEM32/Atok31De.dll"
    ln -sfn "$STUB_ATOK31DE" "$WIN_SYSWOW64/Atok31De.dll"
  fi
  # Real ATOK engine DLLs (dictionary/morphology/API + on-memory-manager deps),
  # 32-bit from SysWOW64. Required for ATOK31OM.EXE and the engine client path.
  for dep in ATOKLIB.DLL ATOK31MP.DLL; do
    if [[ -f "$atok_syswow64/$dep" ]]; then
      ln -sfn "$atok_syswow64/$dep" "$WIN_SYSWOW64/$dep"
      ln -sfn "$atok_syswow64/$dep" "$WIN_SYSTEM32/$dep"
    fi
  done
  if [[ -f "$STUB_ATOK_NSSHIM" ]]; then
    ln -sfn "$STUB_ATOK_NSSHIM" "$WIN_SYSTEM32/AtNsShim.dll"
    ln -sfn "$STUB_ATOK_NSSHIM" "$WIN_SYSWOW64/AtNsShim.dll"
  fi
  if [[ -f "$STUB_MSCTF_SHIM" ]]; then
    ln -sfn "$STUB_MSCTF_SHIM" "$WIN_SYSTEM32/msctf.dll"
    ln -sfn "$STUB_MSCTF_SHIM" "$WIN_SYSWOW64/msctf.dll"
    # MsctfShim loads msctf-helper.dll from its own module dir (system32 when
    # msctf.dll is loaded from there), so the helper must live alongside it.
    # Symlink eagerly (it may be a dangling link until build-msctf-shim.sh runs).
    ln -sfn "${STUB_MSCTF_SHIM%/*}/msctf-helper.dll" "$WIN_SYSTEM32/msctf-helper.dll"
    ln -sfn "${STUB_MSCTF_SHIM%/*}/msctf-helper.dll" "$WIN_SYSWOW64/msctf-helper.dll"
  fi
  if wine_root=$(wine_build_root); then
    while IFS= read -r pe; do
      if [[ "$(basename "$pe")" == "msctf.dll" ]]; then
        continue
      fi
      ln -sfn "$pe" "$WIN_SYSTEM32/$(basename "$pe")"
    done < <(find "$wine_root/dlls" "$wine_root/programs" -path '*/i386-windows/*' -type f \( -name '*.dll' -o -name '*.drv' -o -name '*.exe' \))
  fi
}

ensure_prefix() {
  ensure_layout
  if [[ ! -f "$PREFIX/system.reg" ]]; then
    run_wine wineboot.exe -u
  fi
}

register_atok_tip_clsid() {
  local wine_user
  local tip_dll="C:\\Program Files\\JustSystems\\${INSTALL_DIR}\\ATOK31TIP.DLL"
  local hive

  for hive in \
    'HKCU\Software\Classes\CLSID' \
    'HKCU\Software\Classes\Wow6432Node\CLSID' \
    'HKLM\Software\Classes\CLSID' \
    'HKLM\Software\Classes\Wow6432Node\CLSID'
  do
    run_wine reg.exe add "$hive\\$AT_TIP_CLSID\\InprocServer32" /ve /t REG_SZ /d "$tip_dll" /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$hive\\$AT_TIP_CLSID\\InprocServer32" /v ThreadingModel /t REG_SZ /d Apartment /f >/dev/null 2>&1 || true
  done

  wine_user=$(run_wine cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n' | tail -1)
  if [[ -n "$wine_user" ]]; then
    run_wine reg.exe add 'HKCU\Software\Justsystem\Common' /v UserName /t REG_SZ /d "$wine_user" /f >/dev/null 2>&1 || true
  fi
}

registry_stamp_file() {
  printf '%s/.atok-registry-stamp\n' "$PREFIX"
}

registry_bundle_fingerprint() {
  local hklm_reg hkcu_reg vatok_reg tip_reg
  hklm_reg=$(resolve_bundle_file hklm.reg) || return 1
  hkcu_reg=$(resolve_bundle_file hkcu.reg) || return 1
  vatok_reg=$(resolve_bundle_file VATOK31W.REG) || return 1
  tip_reg="$SCRIPT_DIR/at-tip-clsid.reg"
  stat -c '%Y %n' "$hklm_reg" "$hkcu_reg" "$vatok_reg" "$tip_reg" 2>/dev/null | sort
}

registry_vendor_fingerprint() {
  local hklm_reg hkcu_reg vatok_reg
  hklm_reg=$(resolve_bundle_file hklm.reg) || return 1
  hkcu_reg=$(resolve_bundle_file hkcu.reg) || return 1
  vatok_reg=$(resolve_bundle_file VATOK31W.REG) || return 1
  stat -c '%Y %n' "$hklm_reg" "$hkcu_reg" "$vatok_reg" 2>/dev/null | sort
}

registry_import_is_current() {
  local stamp current saved vendor line

  [[ "${AT_FORCE_REGISTRY_IMPORT:-0}" == 1 ]] && return 1
  stamp=$(registry_stamp_file)
  [[ -f "$stamp" ]] || return 1
  current=$(registry_bundle_fingerprint) || return 1
  saved=$(<"$stamp")
  if [[ "$current" == "$saved" ]]; then
    return 0
  fi

  vendor=$(registry_vendor_fingerprint) || return 1
  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    if ! grep -Fxq "$line" "$stamp"; then
      return 1
    fi
  done <<<"$vendor"
  return 0
}

write_registry_stamp() {
  registry_bundle_fingerprint >"$(registry_stamp_file)"
}

# The minimal vendor hklm.reg leaves ATOK's conversion engine degraded: the
# reconversion/candidate-list APIs fail (ITfFnReconversion::QueryRange returns
# E_INVALIDARG, OM/DV throw registry_access faults, CE counter frozen). Importing
# the full host HKLM\SOFTWARE\WOW6432Node\Justsystem\ATOK\31.0 tree fixes it —
# with it, GetReconversion returns the real candidate list (e.g. にほんご ->
# 日本語, にほんご). Idempotent per prefix; no-op if the export is absent.
import_host_atok_registry() {
  local repo_root host_hklm host_hkcu marker
  repo_root=$(cd -- "$SCRIPT_DIR/.." && pwd)
  host_hklm="$repo_root/recon/captures/host-hklm-wow-atok31.reg"
  host_hkcu="$repo_root/recon/captures/host-hkcu-atok.reg"
  [[ -f "$host_hklm" ]] || return 0
  marker="$PREFIX/.atok-host-registry-imported"
  if [[ -f "$marker" && "${AT_FORCE_REGISTRY_IMPORT:-0}" != 1 ]]; then
    return 0
  fi
  echo "==> importing full host ATOK engine registry (enables reconversion/candidate list)"
  run_wine regedit.exe /S "Z:${host_hklm}"
  [[ -f "$host_hkcu" ]] && run_wine regedit.exe /S "Z:${host_hkcu}"
  : >"$marker"
}

ensure_atok_setup_registry() {
  local hive
  local atok_dir='C:\Program Files\JustSystems\ATOK31T29'
  local vatok_dir='C:\Program Files\JustSystems\ATOK31T29\VATOK'
  local appdata_dir='C:\users\dev\AppData\Roaming\Justsystem'
  local localappdata_dir='C:\users\dev\AppData\Local\Justsystem'
  for hive in \
    'HKCU\Software\Justsystem\ATOK\31.0\Setup' \
    'HKCU\Software\Wow6432Node\Justsystem\ATOK\31.0\Setup' \
    'HKLM\Software\Justsystem\ATOK\31.0\Setup' \
    'HKLM\Software\Wow6432Node\Justsystem\ATOK\31.0\Setup'
  do
    run_wine reg.exe add "$hive" /v TextService31.2 /t REG_SZ /d Enabled /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$hive" /v TextService /t REG_SZ /d Enabled /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$hive" /v Master /t REG_DWORD /d 1 /f >/dev/null 2>&1 || true
  done
  for hive in \
    'HKLM\Software\JustSystem\Atok\SETUP\Folder' \
    'HKLM\Software\Wow6432Node\JustSystem\Atok\SETUP\Folder'
  do
    run_wine reg.exe add "$hive" /v Atok31 /t REG_SZ /d "$atok_dir" /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$hive" /v Atok31Vatok /t REG_SZ /d "$vatok_dir" /f >/dev/null 2>&1 || true
  done
  for hive in \
    'HKLM\Software\JustSystem\Common\SETUP\Folder' \
    'HKLM\Software\Wow6432Node\JustSystem\Common\SETUP\Folder'
  do
    run_wine reg.exe add "$hive" /v ImageAppDataFolder /t REG_SZ /d "$appdata_dir" /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$hive" /v ImageLocalDataFolder /t REG_SZ /d "$localappdata_dir" /f >/dev/null 2>&1 || true
  done
  for hive in \
    'HKLM\Software\JustSystem\Common\UserCstFlag' \
    'HKLM\Software\Wow6432Node\JustSystem\Common\UserCstFlag'
  do
    run_wine reg.exe add "$hive" /v UseDesktopDataFolder /t REG_DWORD /d 0 /f >/dev/null 2>&1 || true
  done
}

import_registry() {
  local hklm_reg hkcu_reg vatok_reg
  local appinit_dlls='C:\\windows\\system32\\mscoree.dll'

  if registry_import_is_current; then
    echo "==> registry import up to date (set AT_FORCE_REGISTRY_IMPORT=1 to redo)"
    import_host_atok_registry
    ensure_atok_setup_registry
    register_atok_tip_clsid
    return 0
  fi

  echo "==> importing ATOK registry (first run may take several minutes)..."

  hklm_reg=$(resolve_bundle_file hklm.reg) || {
    echo "missing hklm.reg in $LOCAL_ROOT or $HOST_ROOT" >&2
    exit 1
  }
  hkcu_reg=$(resolve_bundle_file hkcu.reg) || {
    echo "missing hkcu.reg in $LOCAL_ROOT or $HOST_ROOT" >&2
    exit 1
  }
  vatok_reg=$(resolve_bundle_file VATOK31W.REG) || {
    echo "missing VATOK31W.REG in $LOCAL_ROOT or $HOST_ROOT" >&2
    exit 1
  }

  run_wine regedit.exe /S "Z:${hklm_reg}"
  run_wine regedit.exe /S "Z:${hkcu_reg}"
  run_wine regedit.exe /S "Z:${vatok_reg}"
  import_host_atok_registry
  ensure_atok_setup_registry
  if [[ -f "$STUB_ATOK_NSSHIM" ]]; then
    appinit_dlls+=' C:\\windows\\system32\\AtNsShim.dll'
  fi
  run_wine reg.exe add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d x11 /f >/dev/null 2>&1 || true
  for hive in 'HKCU\Software\Wine\DllOverrides' 'HKLM\Software\Wine\DllOverrides'; do
    run_wine reg.exe add "$hive" /v msctf /t REG_SZ /d native,builtin /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$hive" /v msctfp /t REG_SZ /d native,builtin /f >/dev/null 2>&1 || true
  done
  for clsid in "${MSCTF_COM_CLASSES[@]}"; do
    for hive in \
      'HKCR' \
      'HKCU\Software\Classes' \
      'HKLM\Software\Classes' \
      'HKCU\Software\Classes\Wow6432Node' \
      'HKLM\Software\Classes\Wow6432Node'
    do
      run_wine reg.exe add "$hive\\CLSID\\$clsid\\InprocServer32" /ve /t REG_SZ /d 'C:\windows\system32\msctf.dll' /f >/dev/null 2>&1 || true
      run_wine reg.exe add "$hive\\CLSID\\$clsid\\InprocServer32" /v ThreadingModel /t REG_SZ /d Apartment /f >/dev/null 2>&1 || true
    done
  done
  # Load Wine's mscoree before ATOK starts so the backend sees CLR present.
  for key in \
    'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Windows' \
    'HKLM\Software\Wow6432Node\Microsoft\Windows NT\CurrentVersion\Windows'
  do
    run_wine reg.exe add "$key" /v LoadAppInit_DLLs /t REG_DWORD /d 1 /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$key" /v RequireSignedAppInit_DLLs /t REG_DWORD /d 0 /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$key" /v AppInit_DLLs /t REG_SZ /d "$appinit_dlls" /f >/dev/null 2>&1 || true
  done

  # ATOK's DllRegisterServer only creates the COM CLSID under Wine. Wine msctf
  # enumerates/activates TIPs from these TSF profile keys.
  for key in \
    "HKLM\\Software\\Microsoft\\CTF\\TIP\\$AT_TIP_CLSID\\LanguageProfile\\$LANGID_JA_HEX\\$AT_PROFILE_GUID" \
    "HKCU\\Software\\Microsoft\\CTF\\TIP\\$AT_TIP_CLSID\\LanguageProfile\\$LANGID_JA_HEX\\$AT_PROFILE_GUID"
  do
    run_wine reg.exe add "$key" /v Enable /t REG_DWORD /d 1 /f >/dev/null 2>&1 || true
  done
  run_wine reg.exe add "HKCU\\Software\\Microsoft\\CTF\\Assemblies\\$LANGID_JA_HEX\\$TSF_KEYBOARD_CATID" \
    /v Default /t REG_SZ /d "$AT_TIP_CLSID" /f >/dev/null 2>&1 || true
  run_wine reg.exe add "HKCU\\Software\\Microsoft\\CTF\\Assemblies\\$LANGID_JA_HEX\\$TSF_KEYBOARD_CATID" \
    /v Profile /t REG_SZ /d "$AT_PROFILE_GUID" /f >/dev/null 2>&1 || true

  register_atok_tip_clsid
  write_registry_stamp
  echo "==> registry import complete"
}

launch_exe() {
  local exe=${1:-ATOK31MN.EXE}
  shift || true
  run_wine "$(resolve_bundle_file "$exe")" "$@"
}

launch_exe_in_install_root() {
  local exe=$1
  shift || true
  local install_root

  install_root=$(resolve_install_root) || {
    echo "could not locate ATOK install tree under $LOCAL_ROOT or $HOST_ROOT" >&2
    return 1
  }

  (
    cd "$install_root"
    run_wine "./$exe" "$@"
  )
}

run_jsflt_regserver_probe() {
  local install_root

  install_root=$(resolve_install_root) || {
    echo "could not locate ATOK install tree under $LOCAL_ROOT or $HOST_ROOT" >&2
    return 1
  }

  (
    cd "$install_root"
    set +e
    for cmd in Regserver /RegServer Register /Register; do
      printf '=== JSFLT %s ===\n' "$cmd"
      env WINEPREFIX="$PREFIX" "$WINE_BIN" ./JSFLT.exe "$cmd"
      printf 'exit %s\n' "$?"
    done
  )
}

trace_jsflt_setup() {
  local out_dir=${AT_TRACE_OUTDIR:-"$PWD/recon/captures/jsflt-setup-$(date +%Y%m%d-%H%M%S)"}
  local install_root log rc

  install_root=$(resolve_install_root) || {
    echo "could not locate ATOK install tree under $LOCAL_ROOT or $HOST_ROOT" >&2
    return 1
  }

  mkdir -p "$out_dir"
  log="$out_dir/JSFLT.setup.log"
  timeout "${AT_TRACE_TIMEOUT:-15s}" \
    bash -lc '
      cd "$1" &&
      WINEPREFIX="$2" WINEDEBUG=+server,+msg,+win,+loaddll,+msi,+setupapi,+reg "$3" "./JSFLT.exe" Regserver
    ' _ "$install_root" "$PREFIX" "$WINE_BIN" 2>&1 |
    tee "$log" |
    rg -i 'msi|setupapi|do_file_copyW|do_copy|regserver|register|unregister|create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|create_window|register_class|find_window|send_message|post_message|peek_message|get_message|ATOK|JSFLT|ATFSVR|msctf|tip|Composition|CoCreateInstance|LoadLibraryW|CreateProcessW' || true
  rc=${PIPESTATUS[0]}
  printf 'saved %s\n' "$log"
  printf 'exit %s\n' "$rc"
}

trace_jsflt_boot() {
  local prefix=${AT_JSFLT_BOOT_PREFIX:-"$PWD/.wine-atok-jsflt-boot-$(date +%Y%m%d-%H%M%S)"}
  local out_dir=${AT_TRACE_OUTDIR:-"$PWD/recon/captures/jsflt-boot-$(date +%Y%m%d-%H%M%S)"}
  local install_root log rc

  install_root=$(resolve_install_root) || {
    echo "could not locate ATOK install tree under $LOCAL_ROOT or $HOST_ROOT" >&2
    return 1
  }

  mkdir -p "$out_dir"
  log="$out_dir/JSFLT.boot.log"
  if [[ "${AT_BOOTSTRAP_WINE:-1}" != 0 ]]; then
    rm -rf "$prefix"
    env WINEPREFIX="$prefix" "$WINE_BIN" boot >/dev/null 2>&1 || true
  fi
  timeout "${AT_TRACE_TIMEOUT:-15s}" \
    bash -lc '
      cd "$1" &&
      WINEPREFIX="$2" WINEDEBUG=+server,+msg,+win,+loaddll,+msi,+setupapi,+reg "$3" "./JSFLT.exe" Regserver
    ' _ "$install_root" "$prefix" "$WINE_BIN" 2>&1 |
    tee "$log" |
    rg -i 'msi|setupapi|do_file_copyW|do_copy|regserver|register|unregister|create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|create_window|register_class|find_window|send_message|post_message|peek_message|get_message|ATOK|JSFLT|ATFSVR|msctf|tip|Composition|CoCreateInstance|LoadLibraryW|CreateProcessW' || true
  rc=${PIPESTATUS[0]}
  printf 'saved %s\n' "$log"
  printf 'exit %s\n' "$rc"
}

trace_jsflt_boot_raw() {
  env AT_BOOTSTRAP_WINE=0 "$SCRIPT_DIR/at.sh" tracejsfltboot
}

trace_jsflt_boundary_compare() {
  local boot_before boot_after raw_before raw_after boot_log raw_log

  boot_before=$(ls -td "$PWD"/recon/captures/jsflt-boot-* 2>/dev/null | head -n 1 || true)
  trace_jsflt_boot >/dev/null
  boot_after=$(ls -td "$PWD"/recon/captures/jsflt-boot-* 2>/dev/null | head -n 1 || true)
  if [[ -n "$boot_after" && "$boot_after" != "$boot_before" ]]; then
    boot_log="$boot_after/JSFLT.boot.log"
  else
    boot_log="$boot_after/JSFLT.boot.log"
  fi

  raw_before=$(ls -td "$PWD"/recon/captures/jsflt-boot-* 2>/dev/null | head -n 1 || true)
  trace_jsflt_boot_raw >/dev/null
  raw_after=$(ls -td "$PWD"/recon/captures/jsflt-boot-* 2>/dev/null | head -n 1 || true)
  if [[ -n "$raw_after" && "$raw_after" != "$raw_before" ]]; then
    raw_log="$raw_after/JSFLT.boot.log"
  else
    raw_log="$raw_after/JSFLT.boot.log"
  fi

  printf '\n=== JSFLT bootstrapped markers ===\n'
  if ! rg -n 'start_rpcss|RpcSs|MSIMEService|setupapi:do_file_copyW|err:ole:start_rpcss' "$boot_log"; then
    echo '(none)'
  fi
  printf '\n=== JSFLT raw markers ===\n'
  if ! rg -n 'start_rpcss|RpcSs|MSIMEService|setupapi:do_file_copyW|err:ole:start_rpcss' "$raw_log"; then
    echo '(none)'
  fi
}

trace_jsflt_compare() {
  local shared_prefix=${AT_WINEPREFIX:-"$PWD/.wine-atok-local"}
  local fresh_prefix=${AT_JSFLT_FRESH_PREFIX:-"$PWD/.wine-atok-jsflt-fresh-$(date +%Y%m%d-%H%M%S)"}

  printf '\n=== JSFLT shared prefix ===\n'
  env AT_WINEPREFIX="$shared_prefix" "$SCRIPT_DIR/at.sh" tracejsflt
  printf '\n=== JSFLT fresh prefix ===\n'
  printf 'fresh prefix: %s\n' "$fresh_prefix"
  env AT_WINEPREFIX="$fresh_prefix" "$SCRIPT_DIR/at.sh" tracejsflt
}

run_tipload_snapshot() {
  local snapshot_dir=${AT_TIPLOAD_SNAPSHOT_DIR:-"$PWD/recon/captures/tipruntime-snapshot-$(date +%Y%m%d-%H%M%S)"}
  local snapshot_log="$snapshot_dir/tipruntime.log"
  local run_out="$snapshot_dir/tipsnapshot.out"
  local rc=0
  cleanup_snapshot() {
    local status=$?
    if [[ $rc -eq 0 ]]; then
      rc=$status
    fi
    if [[ -f "$PWD/tipruntime.log" ]]; then
      cp -f "$PWD/tipruntime.log" "$snapshot_log"
    elif [[ -f "$run_out" ]]; then
      cp -f "$run_out" "$snapshot_log"
    elif [[ ! -f "$snapshot_log" ]]; then
      cat >"$snapshot_log" <<EOF
AtTipLoad snapshot did not produce a runtime log.
exit $rc
captured $run_out
EOF
    fi
    if [[ -f "$snapshot_log" && $rc -ne 0 ]]; then
      printf '\n[tipruntime snapshot exit %s]\n' "$rc" >>"$snapshot_log"
    fi
    if [[ -f "$snapshot_log" ]]; then
      cp -f "$snapshot_log" "$PWD/recon/captures/tipruntime-snapshot-latest.log"
    fi
  }

  mkdir -p "$snapshot_dir"
  trap cleanup_snapshot EXIT
  if {
    ensure_prefix
    import_registry
    bash "$SCRIPT_DIR/build-msctf-shim.sh"
    bash "$SCRIPT_DIR/build-at-tipload.sh"
    AT_TIPLOAD_RUNTIME=1 AT_TIPLOAD_SNAPSHOT_ONLY=1 run_wine "$PWD/native/AtTipLoad/AtTipLoad.exe"
  } >"$run_out" 2>&1; then
    :
  else
    rc=$?
  fi
  cleanup_snapshot
  trap - EXIT
  return "$rc"
}

run_tmex_probe() {
  local target_pid=()

  ensure_prefix
  import_registry
  bash "$SCRIPT_DIR/build-at-ctl.sh"
  if [[ $# -gt 0 ]]; then
    target_pid=("$1")
    shift
  fi
  run_wine "$PWD/native/AtCtl/AtCtl.exe" tip "${target_pid[@]}" tmex "$@"
}

run_tmex_snapshot_probe() {
  local target_pid=()

  ensure_prefix
  import_registry
  bash "$SCRIPT_DIR/build-at-ctl.sh"
  if [[ $# -gt 0 ]]; then
    target_pid=("$1")
    shift
  fi
  run_wine "$PWD/native/AtCtl/AtCtl.exe" tip "${target_pid[@]}" tmexsnap "$@"
}

run_manualtip_snapshot_probe() {
  local target_pid=()

  ensure_prefix
  import_registry
  bash "$SCRIPT_DIR/build-at-ctl.sh"
  if [[ $# -gt 0 ]]; then
    target_pid=("$1")
    shift
  fi
  run_wine "$PWD/native/AtCtl/AtCtl.exe" tip "${target_pid[@]}" manualtipsnap "$@"
}

start_shell() {
  # Launch a simple Wine app window on the current desktop so the GUI is visible.
  exec env WINEPREFIX="$PREFIX" "$WINE_BIN" notepad
}

trace_exe() {
  local exe=${1:-ATOK31MN.EXE}
  shift || true
  local timeout_s=${AT_TRACE_TIMEOUT:-10s}
  timeout "$timeout_s" \
    env WINEPREFIX="$PREFIX" WINEDEBUG=+server "$WINE_BIN" "$(resolve_bundle_file "$exe")" "$@" 2>&1 |
    rg -i 'create_(mutex|event|named_pipe|file_mapping)|open_(mutex|event|named_pipe|file_mapping)|BaseNamedObjects|\\\\\\.\\\\pipe|ATOK|JSFLT|ATFSVR|svcctl|NtControlPipe'
}

trace_ipc_exe() {
  local exe=${1:-ATOK31MN.EXE}
  shift || true
  local timeout_s=${AT_TRACE_TIMEOUT:-10s}
  local target

  if [[ "$exe" == "ATFSVR31.EXE" ]]; then
    bash "$SCRIPT_DIR/build-at-launch.sh"
    target=("$PWD/native/AtLaunch/AtLaunch.exe" "$(resolve_bundle_file ATFSVR31.EXE)")
  elif [[ "$exe" == "JSFLT.exe" ]]; then
    target=("$(resolve_bundle_file "$exe")")
  else
    target=("$(resolve_bundle_file "$exe")")
  fi

  timeout "$timeout_s" \
    env WINEPREFIX="$PREFIX" WINEDEBUG=+server,+msg,+win,+loaddll "$WINE_BIN" "${target[@]}" "$@" 2>&1 |
    rg -i 'create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|create_window|register_class|find_window|send_message|post_message|peek_message|get_message|setwindowtheme|setwindowlong|setwindowpos|ATOK|JSFLT|ATFSVR|msctf|tip|LangBarUI|InputConvert|Reconversion|SearchCandidateProvider|DocumentFeed|Composition'
}

trace_ipc_all() {
  local out_dir=${AT_TRACE_OUTDIR:-"$PWD/recon/captures/wine-trace-$(date +%Y%m%d-%H%M%S)"}
  local exe log rc install_root

  mkdir -p "$out_dir"
  printf '\n=== traceipc ATOK31MN.EXE ===\n'
  log="$out_dir/ATOK31MN.trace.log"
  timeout "${AT_TRACE_TIMEOUT:-10s}" \
    env WINEPREFIX="$PREFIX" WINEDEBUG=+server,+msg,+win,+loaddll "$WINE_BIN" "$(resolve_bundle_file ATOK31MN.EXE)" 2>&1 |
    tee "$log" |
    rg -i 'create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|create_window|register_class|find_window|send_message|post_message|peek_message|get_message|setwindowtheme|setwindowlong|setwindowpos|ATOK|JSFLT|ATFSVR|msctf|tip|LangBarUI|InputConvert|Reconversion|SearchCandidateProvider|DocumentFeed|Composition' || true
  rc=${PIPESTATUS[0]}
  printf 'saved %s\n' "$log"
  printf 'exit %s\n' "$rc"
  printf '\n=== traceipc ATFSVR31.EXE ===\n'
  log="$out_dir/ATFSVR31.trace.log"
  bash "$SCRIPT_DIR/build-at-launch.sh"
  timeout "${AT_TRACE_TIMEOUT:-10s}" \
    env WINEPREFIX="$PREFIX" WINEDEBUG=+server,+msg,+win,+loaddll "$WINE_BIN" "$PWD/native/AtLaunch/AtLaunch.exe" "$(resolve_bundle_file ATFSVR31.EXE)" 2>&1 |
    tee "$log" |
    rg -i 'create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|create_window|register_class|find_window|send_message|post_message|peek_message|get_message|setwindowtheme|setwindowlong|setwindowpos|ATOK|JSFLT|ATFSVR|msctf|tip|LangBarUI|InputConvert|Reconversion|SearchCandidateProvider|DocumentFeed|Composition' || true
  rc=${PIPESTATUS[0]}
  printf 'saved %s\n' "$log"
  printf 'exit %s\n' "$rc"
  printf '\n=== traceipc JSFLT.exe Regserver ===\n'
  log="$out_dir/JSFLT.trace.log"
  install_root=$(resolve_install_root) || {
    echo "could not locate ATOK install tree under $LOCAL_ROOT or $HOST_ROOT" >&2
    return 1
  }
  timeout "${AT_TRACE_TIMEOUT:-10s}" \
    bash -lc '
      cd "$1" &&
      WINEPREFIX="$2" WINEDEBUG=+server,+msg,+win,+loaddll "$3" "./JSFLT.exe" Regserver
    ' _ "$install_root" "$PREFIX" "$WINE_BIN" 2>&1 |
    tee "$log" |
    rg -i 'create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|create_window|register_class|find_window|send_message|post_message|peek_message|get_message|setwindowtheme|setwindowlong|setwindowpos|ATOK|JSFLT|ATFSVR|msctf|tip|LangBarUI|InputConvert|Reconversion|SearchCandidateProvider|DocumentFeed|Composition' || true
  rc=${PIPESTATUS[0]}
  printf 'saved %s\n' "$log"
  printf 'exit %s\n' "$rc"
  printf '\n=== JSFLT registration probe ===\n'
  run_jsflt_regserver_probe
}

trace_ib_dv() {
  local out_dir=${AT_TRACE_OUTDIR:-"$PWD/recon/captures/wine-trace-ibdv-$(date +%Y%m%d-%H%M%S)"}
  local exe log rc target_path target_win settle wine_debug
  local filter='ATOK31(IB|DV)|AtLaunch|AtNsShim|JsMmf_ATOK31IB_EXEC_DATA_|JsMmfAtok31wLaunchDataMemory|JsMtx_ATOK31ROMATABLE|JsMtxAtok31wSharedMemory|ROMATABLE|OpenMapMem|CManager::Init|create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|BaseNamedObjects|Nt(Create|Open|Map|Query|Set)|svcctl|RpcSs|MSIMEService|err:|warn:'

  mkdir -p "$out_dir"
  bash "$SCRIPT_DIR/build-at-launch.sh"
  bash "$SCRIPT_DIR/build-at-nsshim.sh"

  for exe in ATOK31IB.EXE ATOK31DV.EXE; do
    printf '\n=== traceibdv %s ===\n' "$exe"
    log="$out_dir/$exe.trace.log"
    target_path=$(resolve_bundle_file "$exe")
    target_win="Z:$target_path"
    target_win=${target_win//\//\\}
    settle=${AT_TRACE_SETTLE:-8}
    wine_debug=${AT_TRACE_WINEDEBUG:-"+server,+file,+loaddll,+process,+seh"}
    set +e
    timeout "${AT_TRACE_TIMEOUT:-20s}" bash -lc '
      prefix=$1
      debug=$2
      wine_bin=$3
      launcher=$4
      target=$5
      exe=$6
      settle=$7

      WINEPREFIX="$prefix" WINEDEBUG="$debug" "$wine_bin" "$launcher" "$target" &
      launcher_pid=$!
      sleep "$settle"
      WINEPREFIX="$prefix" "$wine_bin" cmd.exe /c taskkill /IM "$exe" /F >/dev/null 2>&1 || true
      wait "$launcher_pid" || true
    ' _ "$PREFIX" "$wine_debug" "$WINE_BIN" "$PWD/native/AtLaunch/AtLaunch.exe" "$target_win" "$exe" "$settle" >"$log" 2>&1
    rc=$?
    set -e
    rg -i "$filter" "$log" || true
    printf 'saved %s\n' "$log"
    printf 'exit %s\n' "$rc"
  done

  printf '\n=== traceibdv object summary ===\n'
  for exe in ATOK31IB.EXE ATOK31DV.EXE; do
    log="$out_dir/$exe.trace.log"
    printf '\n--- %s ---\n' "$exe"
    rg -n -i 'JsMmf_ATOK31IB_EXEC_DATA_|JsMmfAtok31wLaunchDataMemory|JsMtx_ATOK31ROMATABLE|JsMtxAtok31wSharedMemory|ROMATABLE|OpenMapMem|create_(mutex|event|named_pipe|file_mapping)|open_(mutex|event|named_pipe|file_mapping)|BaseNamedObjects|svcctl|RpcSs|MSIMEService|err:|warn:' "$log" || true
  done
  printf '\nsaved trace directory %s\n' "$out_dir"
}

trace_ipc_all_compare() {
  local out_dir before after atok_log fsvr_log

  before=$(ls -td "$PWD"/recon/captures/wine-trace-* 2>/dev/null | head -n 1 || true)
  trace_ipc_all >/dev/null
  after=$(ls -td "$PWD"/recon/captures/wine-trace-* 2>/dev/null | head -n 1 || true)
  if [[ -n "$after" ]]; then
    out_dir="$after"
  else
    out_dir="$before"
  fi

  atok_log="$out_dir/ATOK31MN.trace.log"
  fsvr_log="$out_dir/ATFSVR31.trace.log"

  printf '\n=== ATOK31MN markers ===\n'
  rg -n 'AtokBoundaryName|AtokPrivateNamespace|BaseNamedObjects|JsMmf|JsMtx|MSIMEService|WM_PARENTNOTIFY|create_named_pipe|open_named_pipe|svcctl|NtControlPipe|ATOK31TRAYMANAGER' "$atok_log" || true
  printf '\n=== ATFSVR31 markers ===\n'
  rg -n 'AtLaunch|AtokPrivateNamespace|BaseNamedObjects|JsMmf|JsMtx|mscoree.dll|ATFSVR31.EXE|create_named_pipe|open_named_pipe|svcctl|NtControlPipe|MSIMEService' "$fsvr_log" || true
}

trace_ipc_all_objects() {
  local out_dir before after atok_log fsvr_log

  before=$(ls -td "$PWD"/recon/captures/wine-trace-* 2>/dev/null | head -n 1 || true)
  trace_ipc_all >/dev/null
  after=$(ls -td "$PWD"/recon/captures/wine-trace-* 2>/dev/null | head -n 1 || true)
  if [[ -n "$after" ]]; then
    out_dir="$after"
  else
    out_dir="$before"
  fi

  atok_log="$out_dir/ATOK31MN.trace.log"
  fsvr_log="$out_dir/ATFSVR31.trace.log"

  printf '\n=== ATOK31MN object markers ===\n'
  rg -n 'create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|BaseNamedObjects|AtokBoundaryName|AtokPrivateNamespace|JsMmf|JsMtx|ATOK31TRAYMANAGER|MSIMEService' "$atok_log" || true
  printf '\n=== ATFSVR31 object markers ===\n'
  rg -n 'create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|close_private_namespace|addsid|create_boundary_descriptor|BaseNamedObjects|AtLaunch|AtokPrivateNamespace|JsMmf|JsMtx|mscoree.dll|ATFSVR31.EXE|MSIMEService' "$fsvr_log" || true
}

latest_trace_bundle_dir() {
  ls -td "$PWD"/recon/captures/wine-trace-* 2>/dev/null | head -n 1 || true
}

trace_ipc_all_names() {
  local out_dir before after atok_log fsvr_log jsflt_log summary_dir summary_file
  local -a patterns=(
    'AtokBoundaryName'
    'AtokPrivateNamespace'
    'BaseNamedObjects'
    'JsMmf'
    'JsMtx'
    'AtLaunch'
    'MSIMEService'
    'ATOK31TRAYMANAGER'
    'ATFSVR31.EXE'
    'create_(mutex|event|named_pipe|file_mapping|private_namespace|boundary_descriptor)'
    'open_(mutex|event|named_pipe|file_mapping|private_namespace)'
  )
  local -a boundary_patterns=(
    'CreateBoundaryDescriptorW'
    'SetNamedSecurityInfoW'
    'GetNamedSecurityInfoW'
    'AddSIDToBoundaryDescriptor'
  )

  before=$(latest_trace_bundle_dir)
  if [[ "${AT_TRACE_REUSE:-0}" != 1 ]]; then
    trace_ipc_all >/dev/null || true
  fi
  after=$(latest_trace_bundle_dir)
  out_dir=${after:-$before}
  if [[ -z "$out_dir" ]]; then
    echo "could not find a trace bundle under recon/captures/wine-trace-*" >&2
    return 1
  fi

  atok_log="$out_dir/ATOK31MN.trace.log"
  fsvr_log="$out_dir/ATFSVR31.trace.log"
  jsflt_log="$out_dir/JSFLT.trace.log"
  summary_dir=${AT_TRACE_SUMMARY_DIR:-"$PWD/recon/captures/traceallnames-$(date +%Y%m%d-%H%M%S)"}
  mkdir -p "$summary_dir"
  summary_file="$summary_dir/traceallnames.txt"
  : >"$summary_file"

  summarize_line() {
    printf '%s\n' "$1" | tee -a "$summary_file"
  }

  if [[ ! -f "$atok_log" || ! -f "$fsvr_log" || ! -f "$jsflt_log" ]]; then
    echo "missing one or more trace logs in $out_dir" >&2
    return 1
  fi

  summarize_log() {
    local label=$1
    local log=$2
    local pattern

    summarize_line ""
    summarize_line "=== ${label} name presence ==="
    for pattern in "${patterns[@]}"; do
      if rg -q "$pattern" "$log"; then
        summarize_line "present $pattern"
      else
        summarize_line "absent  $pattern"
      fi
    done
  }

  summarize_boundary() {
    local label=$1
    local log=$2
    local pattern

    summarize_line ""
    summarize_line "=== ${label} boundary setup ==="
    for pattern in "${boundary_patterns[@]}"; do
      if rg -q "$pattern" "$log"; then
        summarize_line "present $pattern"
      else
        summarize_line "absent  $pattern"
      fi
    done
  }

  summarize_log ATOK31MN "$atok_log"
  summarize_boundary ATOK31MN "$atok_log"
  summarize_log ATFSVR31 "$fsvr_log"
  summarize_boundary ATFSVR31 "$fsvr_log"
  summarize_log JSFLT "$jsflt_log"
  summarize_boundary JSFLT "$jsflt_log"
  summarize_line ""
  summarize_line "saved summary $summary_file"
  cp -f "$summary_file" "$PWD/recon/captures/traceallnames-latest.txt"
}

trace_ipc_all_delta() {
  local summary_file expected_file delta_dir delta_file name found
  local -a boundary_ns=(
    'AtokBoundaryName'
    'AtokPrivateNamespace'
    'Local\\{C15730E2-145C-4c5e-B005-3BC753F42475}-once-flag'
  )
  local -a shared_memory=(
    'JsMmfAtok31wNewContext'
    'JsMmfAtok31wTimeOutCount'
    'JsMmfAtok31wClipBoardData'
    'JsMmfAtok31wWORDSHAREDDATA'
    'JsMmf_AtokProxySharedMemory'
    'JsMmfAtok31wATOKALG_SHARED'
    'JsMmfAtok31wDecHisSet'
    'JsMtxAtok31wSharedMemory'
    'JsMtxAtok31wStringBoxInfo'
    'JsMmfAtok19rtSharedMemory'
    'JsMmf_ATOK31_TRAYMANAGER'
  )
  local -a processes=(
    'ATOK31MN.EXE'
    'ATFSVR31.EXE'
    'JSFLT.exe'
    'ATOK31DV.EXE'
    'ATOK31OM.EXE'
    'ATOK31IB.EXE'
    'TextInputHost.exe'
    'ctfmon.exe'
    'dllhost.exe'
  )
  local -a boundary_anchors=(
    'native/AtNsShim/AtNsShim.c'
    'native/AtTipLoad/AtTipLoad.c'
    'docs/at-ipc-names.md'
  )
  local -a shared_anchors=(
    'native/AtTipLoad/AtTipLoad.c'
    'native/AtShmDump/AtShmDump.c'
    'docs/at-ipc-names.md'
  )
  local -a process_anchors=(
    'native/AtLaunch/AtLaunch.c'
    'scripts/windows/at-dump.ps1'
    'docs/windows-dump-shopping-list.txt'
  )

  summary_file=$(ls -td "$PWD"/recon/captures/traceallnames-*/traceallnames.txt 2>/dev/null | head -n 1 || true)
  if [[ -z "$summary_file" || ! -f "$summary_file" ]]; then
    echo "missing traceallnames summary under recon/captures/traceallnames-*" >&2
    return 1
  fi

  expected_file="$PWD/docs/at-ipc-names.md"
  if [[ ! -f "$expected_file" ]]; then
    echo "missing expected-name doc: $expected_file" >&2
    return 1
  fi

  delta_dir=${AT_TRACE_SUMMARY_DIR:-"$PWD/recon/captures/traceallnames-delta-$(date +%Y%m%d-%H%M%S)"}
  mkdir -p "$delta_dir"
  delta_file="$delta_dir/traceallnames-delta.txt"
  : >"$delta_file"

  summarize_line() {
    printf '%s\n' "$1" | tee -a "$delta_file"
  }

  summarize_line "source summary: $summary_file"
  summarize_line "expected names from docs: $expected_file and docs/windows-dump-shopping-list.txt"
  summarize_line ""

  emit_group() {
    local title=$1
    shift
    local item

    summarize_line "=== $title ==="
    for item in "$@"; do
      if [[ -z "$item" ]]; then
        continue
      fi
      if rg -Fx -q -- "present $item" "$summary_file"; then
        found="present"
      else
        found="absent"
      fi
      summarize_line "$found $item"
    done
    summarize_line ""
  }

  emit_group 'Boundary / namespace' "${boundary_ns[@]}"
  emit_group 'Shared memory / mutex' "${shared_memory[@]}"
  emit_group 'Processes' "${processes[@]}"

  summarize_line ""
  summarize_line "saved delta $delta_file"

  summarize_line ""
  summarize_line "=== Code anchors ==="
  summarize_line "boundary / namespace: ${boundary_anchors[*]}"
  summarize_line "shared memory / mutex: ${shared_anchors[*]}"
  summarize_line "processes: ${process_anchors[*]}"
  cp -f "$delta_file" "$PWD/recon/captures/traceallnames-delta-latest.txt"
}

main() {
  local cmd=${1:-}
  if [[ -z "$cmd" ]]; then
    usage
    exit 1
  fi
  shift

  case "$cmd" in
    layout)
      ensure_prefix
      ;;
    setup)
      ensure_prefix
      register_atok_tip_clsid
      import_registry
      ;;
    run)
      ensure_prefix
      import_registry
      launch_exe "$@"
      ;;
    trace)
      ensure_prefix
      import_registry
      trace_exe "$@"
      ;;
    traceipc)
      ensure_prefix
      import_registry
      trace_ipc_exe "$@"
      ;;
    traceall)
      ensure_prefix
      import_registry
      trace_ipc_all
      ;;
    traceibdv)
      ensure_prefix
      import_registry
      trace_ib_dv
      ;;
    traceallcmp)
      ensure_prefix
      import_registry
      trace_ipc_all_compare
      ;;
    traceallobj)
      ensure_prefix
      import_registry
      trace_ipc_all_objects
      ;;
    traceallnames)
      if [[ "${AT_TRACE_REUSE:-0}" != 1 ]]; then
        ensure_prefix
        import_registry
      fi
      trace_ipc_all_names
      ;;
    tracealldelta)
      trace_ipc_all_delta
      ;;
    tracejsflt)
      ensure_prefix
      import_registry
      trace_jsflt_setup
      ;;
    tracejsfltboot)
      trace_jsflt_boot
      ;;
    tracejsfltraw)
      trace_jsflt_boot_raw
      ;;
    tracejsfltdiff)
      trace_jsflt_boundary_compare
      ;;
    tracejsfltcmp)
      trace_jsflt_compare
      ;;
    tmex)
      run_tmex_probe "$@"
      ;;
    tmexsnap)
      run_tmex_snapshot_probe "$@"
      ;;
    manualtipsnap)
      run_manualtip_snapshot_probe "$@"
      ;;
    tipsnapshot)
      run_tipload_snapshot
      ;;
    regserver)
      ensure_prefix
      import_registry
      run_jsflt_regserver_probe
      ;;
    shell)
      ensure_prefix
      import_registry
      start_shell
      if [[ $# -gt 0 ]]; then
        launch_exe "$@"
      fi
      ;;
    *)
      usage
      exit 1
      ;;
  esac
}

main "$@"
