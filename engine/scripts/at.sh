#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
WRAPPER="$HERE/at-wine.sh"

if [[ ! -x "$WRAPPER" ]]; then
  echo "missing helper: $WRAPPER" >&2
  exit 1
fi

# External deps live under one parent: $AT_HOME/{wine,at} (default .atzc/).
AT_HOME=${AT_HOME:-"$PWD/.atzc"}
LOCAL_ROOT=${AT_LOCAL_ROOT:-"$AT_HOME/at"}
HOST_ROOT=${AT_HOST_ROOT:-"$AT_HOME/at"}
INSTALL_DIR=${AT_INSTALL_DIR:-ATOK31T29}
PREFIX=${AT_WINEPREFIX:-"$PWD/.wine-atok-local"}
# Prefer the pinned Wine (fetch-wine.sh) if present; else AT_WINE; else system.
AT_WINE_PIN=""
# shellcheck source=scripts/wine-pin.sh
source "$HERE/wine-pin.sh"
at_resolve_wine_pin "$AT_HOME/wine"
if [[ -n "${AT_WINE:-}" ]]; then WINE_BIN=$AT_WINE; WINESERVER_BIN=wineserver
elif [[ -n "$AT_WINE_PIN" ]]; then WINE_BIN="$AT_WINE_PIN/bin/wine"; WINESERVER_BIN="$AT_WINE_PIN/bin/wineserver"
else WINE_BIN=wine; WINESERVER_BIN=wineserver; fi
DEFAULT_EXE=${AT_DEFAULT_EXE:-ATOK31MN.EXE}

ensure_runtime_dir() {
  local dir="${XDG_RUNTIME_DIR:-}"
  if [[ -z "$dir" ]] || ! mkdir -p "$dir" 2>/dev/null || ! touch "$dir/.atok-write-test" 2>/dev/null; then
    dir="/tmp/runtime-$(id -u)-atok"
    mkdir -p "$dir"
    chmod 700 "$dir" 2>/dev/null || true
    export XDG_RUNTIME_DIR="$dir"
  else
    rm -f "$dir/.atok-write-test" 2>/dev/null || true
  fi
}

ensure_runtime_dir

usage() {
  cat <<'EOF'
usage: scripts/at.sh [command] [args...]

Short commands:
  run [exe...]       Launch ATOK31MN.EXE by default.
  mn [args...]       Alias for the main launcher.
  fs [args...]       Launch ATFSVR31.EXE.
  flt [args...]      Run JSFLT.exe Regserver.
  tipreg             Register ATOK31TIP.DLL with regsvr32.
  nsshim             Build the Wine private-namespace shim DLL.
  build              Build runtime components + AtCtl, no launch (warm-loop prep).
  tipruntime         Launch AtTipLoad in persistent runtime mode.
  daemon-up          One-time relay bring-up: prefix + build + warm servers
                     (no TIP). For ../atzc-server. Run once.
  daemon-once        One conversion via a fresh single-shot TIP against warm
                     servers (ATD line protocol on stdin/stdout). Per request.
  drive <commands>   Send ;-separated runtime commands to the resident tipruntime
                     and print the new tipruntime.log lines (fast inner loop;
                     e.g. drive "reset; type kisha; reading; reconv"). No rebuild.
  tmexsnap [pid]     Send the `tmexsnap` runtime command through the resident TipLoad window.
  tmex [pid]         Send the TMEx state-dump probe through the resident TipLoad window.
  manualtipsnap [pid] Send the `manualtipsnap` runtime command through the resident TipLoad window.
  tipsnapshot        Launch AtTipLoad in one-shot snapshot mode, exit after logging the startup snapshot, and save recon/captures/tipruntime-snapshot-latest.log.
  scan               Register the TIP DLL, then dump filtered CTF/CLSID keys.
  trace [target...]  Trace the default launcher or a selected target.
  ipc [target...]    Trace server objects plus window/message traffic.
  traceall           Trace ATOK31MN.EXE, ATFSVR31.EXE, and JSFLT.exe.
  traceibdv          Trace ATOK31IB.EXE and ATOK31DV.EXE startup around missing IPC objects.
  traceallcmp        Compare the latest ATOK31MN.EXE and ATFSVR31.EXE trace markers.
  traceallobj        Compare the latest ATOK31MN.EXE and ATFSVR31.EXE object markers.
  traceallnames      Compare the latest trace bundle against the Windows endpoint-name set and boundary-setup markers (set AT_TRACE_REUSE=1 to skip a fresh Wine run and inspect the newest saved bundle; the summary is saved under recon/captures/traceallnames-latest.txt).
  tracealldelta      Compare the saved traceallnames summary against the documented Windows IPC names and write recon/captures/traceallnames-delta-latest.txt.
  tracejsflt         Trace JSFLT.exe setup/setupapi behavior from install tree.
  tracejsfltboot     Trace JSFLT.exe from a fresh prefix bootstrap (set AT_BOOTSTRAP_WINE=0 to skip pre-warming).
  tracejsfltraw      Trace JSFLT.exe from a fresh prefix without pre-warming.
  tracejsfltdiff     Compare the bootstrapped and raw JSFLT service-boundary markers.
  tracejsfltcmp      Trace JSFLT.exe in shared and fresh prefixes.
  probe              Find the TIP window and print ATOK registered messages.
  tipload            Force-load the ATOK TIP COM server (CoCreateInstance).
  shmdump [args...]  Probe known ATOK shared-memory mappings.
  target [mode...]   Build and launch a simple input target window.
  ctl <command...>   Send a runtime control command to the target window.
  traceapp [mode...]  Trace the input target window under Wine.
                     Modes include `nativejp`, `commitjp`, and
                     `postkonnichiha-openjp`.
                     `stubreconv` seeds text and answers IME reconversion
                     and document-feed requests from the client side.
                     `selftestreconv` exercises the reconversion request
                     path without host-side key injection.
  msg <name>         Send a registered message to ATOK31TRAYMANAGER.
  msgtrace <name> [wparam] [lparam]
                     Trace the registered message sender while it talks to ATOK31TRAYMANAGER.
  shell [exe...]     Start explorer.exe on a Wine desktop, optionally launch ATOK.
  setup              Create the prefix and import the registry.

You can also pass a raw executable name, which is treated like `run <exe>`.
EOF
}

wine_env() {
  WINEPREFIX="$PREFIX" "$@"
}

run_wine() {
  # The MsctfShim is installed into the prefix as msctf.dll (with the real Wine
  # msctf renamed to msctf-helper.dll) and the prefix carries a registry
  # DllOverride of msctf=native,builtin. But when WINEDLLOVERRIDES is set in the
  # environment (e.g. the dev container forces mscoree,mshtml= to suppress
  # Gecko/Mono), Wine ignores the registry override for DLLs not named in the
  # env var, so it loads its own builtin msctf and the shim never activates.
  # Always name msctf/msctfp here so the shim loads regardless of the inherited
  # WINEDLLOVERRIDES or Wine version.
  WINEDLLOVERRIDES="msctf=native,builtin;msctfp=native,builtin;${WINEDLLOVERRIDES:-mscoree,mshtml=},winedbg.exe=d" \
  wine_env "$WINE_BIN" "$@"
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

kill_existing_target() {
  ensure_layout
  run_wine cmd.exe /c taskkill /IM AtTarget.exe /F >/dev/null 2>&1 || true
  run_wine cmd.exe /c taskkill /IM atoktarget.exe /F >/dev/null 2>&1 || true
}

kill_existing_tipruntime() {
  ensure_layout
  bash "$HERE/build-at-ctl.sh"
  run_wine "$PWD/native/AtCtl/AtCtl.exe" tip quit >/dev/null 2>&1 || true
  run_wine "$PWD/native/AtCtl/AtCtl.exe" quit >/dev/null 2>&1 || true
  local i
  for i in $(seq 1 10); do
    if ! run_wine "$PWD/native/AtCtl/AtCtl.exe" list 2>/dev/null | rg -q 'AtTipHostFrame|AtTipHost'; then
      break
    fi
    sleep 1
  done
  if run_wine "$PWD/native/AtCtl/AtCtl.exe" list 2>/dev/null | rg -q 'AtTipHostFrame|AtTipHost'; then
    run_wine cmd.exe /c taskkill /IM AtTipLoad.exe /F >/dev/null 2>&1 || true
    sleep 1
  fi
  # prepare_orchestrated_servers launches an ATOK31OM/DV/IB constellation but
  # nothing tears it down at end-of-run, so without a reset each run stacks
  # another set. Multiple ATOK31DV/IB instances then race over the
  # dictionary/CE shared memory and mutexes, making conversion
  # nondeterministic (one run does the full romaji->kanji conversion, the next
  # barely activates). A full wineserver reset guarantees a single fresh
  # constellation per run; prepare_orchestrated_servers restarts it cleanly.
  wine_env "$WINESERVER_BIN" -k >/dev/null 2>&1 || true
  sleep 1
}

ensure_layout() {
  "$WRAPPER" layout
}

run_default() {
  exec "$WRAPPER" run "$DEFAULT_EXE" "$@"
}

run_main() {
  exec "$WRAPPER" run ATOK31MN.EXE "$@"
}

  run_server() {
  ensure_layout
  bash "$HERE/build-at-launch.sh"
  run_wine "$PWD/native/AtLaunch/AtLaunch.exe" "$(resolve_bundle_file ATFSVR31.EXE)" "$@"
}

  run_filter_regserver() {
    exec "$WRAPPER" regserver "$@"
  }

run_nsshim() {
  bash "$HERE/build-at-nsshim.sh"
}

run_probe() {
  ensure_layout
  bash "$HERE/build-at-probe.sh"
  run_wine "$PWD/native/AtProbe/AtProbe.exe"
}

prepare_orchestrated_servers() {
  local tip_clsid='{1314EB53-CACA-4152-A556-A184143202AF}'
  local tip_dll="C:\\Program Files\\JustSystems\\${INSTALL_DIR}\\ATOK31TIP.DLL"
  local om_exe="Z:$LOCAL_ROOT/$INSTALL_DIR/ATOK31OM.EXE"
  om_exe=${om_exe//\//\\}

  ensure_layout

  for udir in "$PREFIX"/drive_c/users/*/AppData/Roaming; do
    [ -d "$(dirname "$udir")" ] || continue
    mkdir -p "$udir"
    [ -d "$LOCAL_ROOT/AppDataRoaming/Justsystem" ] && cp -rn "$LOCAL_ROOT/AppDataRoaming/Justsystem" "$udir/" 2>/dev/null || true
  done

  for hive in 'HKLM\Software\Classes\CLSID' 'HKLM\Software\Classes\Wow6432Node\CLSID' \
              'HKCU\Software\Classes\CLSID' 'HKCU\Software\Classes\Wow6432Node\CLSID'; do
    run_wine reg.exe add "$hive\\$tip_clsid\\InprocServer32" /ve /t REG_SZ /d "$tip_dll" /f >/dev/null 2>&1 || true
    run_wine reg.exe add "$hive\\$tip_clsid\\InprocServer32" /v ThreadingModel /t REG_SZ /d Apartment /f >/dev/null 2>&1 || true
  done

  bash "$HERE/build-at-launch.sh" >/dev/null 2>&1 || true
  bash "$HERE/build-at-probe.sh" >/dev/null 2>&1 || true
  bash "$HERE/build-probeom.sh" >/dev/null 2>&1 || true

  # Keep every ATOK component on the same JustSystems user suffix. The vendor
  # import stores the canonical value as HKCU\...\Common\UserName\(Default),
  # while ATOK31TIP also reads HKCU\...\Common /v UserName.
  local atok_user
  atok_user=${AT_USER_SUFFIX:-}
  if [[ -z "$atok_user" ]]; then
    atok_user=$(run_wine reg query 'HKCU\Software\Justsystem\Common\UserName' 2>/dev/null |
      awk '/REG_SZ/ {print $NF; exit}' | tr -d '\r')
  fi
  if [[ -z "$atok_user" ]]; then
    atok_user=$(run_wine cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n' | tail -1)
  fi
  if [[ -n "$atok_user" ]]; then
    run_wine reg add 'HKCU\Software\Justsystem\Common\UserName' /ve /t REG_SZ /d "$atok_user" /f >/dev/null 2>&1 || true
    run_wine reg add 'HKCU\Software\Justsystem\Common' /v UserName /t REG_SZ /d "$atok_user" /f >/dev/null 2>&1 || true
  fi

  if ! xdpyinfo -display ":0" >/dev/null 2>&1; then
    atok-gui >/dev/null 2>&1 || true
  fi

  run_wine rpcss.exe >/dev/null 2>&1 &
  sleep 3
  local prepare_log="${AT_PREPARE_LOG:-$PWD/recon/captures/prepare-orchestrated-latest.log}"
  mkdir -p "$(dirname "$prepare_log")"
  {
    printf '[prepare] %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '[prepare] wine_user=%s\n' "${wine_user:-}"
    printf '[prepare] om_exe=%s\n' "$om_exe"
    printf '[prepare] components=%s\n' "${AT_PREPARE_COMPONENTS:-ATOK31OM.EXE ATOK31DV.EXE ATOK31IB.EXE ATFSVR31.EXE}"
    printf '[prepare] snapshot_with_om=%s\n' "${AT_SNAPSHOT_WITH_OM:-1}"
  } >"$prepare_log"

  if [[ "${AT_SNAPSHOT_WITH_OM:-1}" == 1 ]] && [[ -f "$PWD/native/AtLaunch/AtLaunch.exe" ]]; then
    local exe exe_path exe_win args sleep_s
    sleep_s=${AT_PREPARE_SLEEP:-4}
    for exe in ${AT_PREPARE_COMPONENTS:-ATOK31OM.EXE ATOK31DV.EXE ATOK31IB.EXE ATFSVR31.EXE}; do
      exe_path="$LOCAL_ROOT/$INSTALL_DIR/$exe"
      if [[ ! -f "$exe_path" ]]; then
        printf '[prepare] missing %s\n' "$exe" >>"$prepare_log"
        continue
      fi
      exe_win="Z:$exe_path"
      exe_win=${exe_win//\//\\}
      args=()
      [[ "$exe" == "ATOK31OM.EXE" ]] && args=(/startup)
      printf '[prepare] launching %s %s\n' "$exe" "${args[*]:-}" >>"$prepare_log"
      run_wine "$PWD/native/AtLaunch/AtLaunch.exe" "$exe_win" "${args[@]}" \
        >"$PWD/recon/captures/server-$exe-latest.log" 2>&1 &
      sleep "$sleep_s"
    done
  fi
  if [[ -x "$PWD/native/ProbeOM/ProbeOM.exe" ]]; then
    run_wine "$PWD/native/ProbeOM/ProbeOM.exe" 2>>"$prepare_log" | tee -a "$prepare_log" | sed 's/^/[ProbeOM] /' || true
  else
    printf '[prepare] ProbeOM.exe missing\n' >>"$prepare_log"
  fi
}

# Build the components a tipload/tipruntime run needs. Skipped when
# AT_SKIP_BUILD=1, so you can re-run with different env or relaunch the warm
# runtime without paying for a clang rebuild each time.
build_runtime_components() {
  if [[ "${AT_SKIP_BUILD:-0}" == 1 ]]; then
    echo "[build] AT_SKIP_BUILD=1 — skipping native rebuild"
    return 0
  fi
  bash "$HERE/build-at-nsshim.sh"
  bash "$HERE/build-msctf-shim.sh"
  bash "$HERE/build-at-tipload.sh"
  bash "$HERE/build-at-de.sh"
}

# Re-assert the MsctfShim msctf.dll symlink in the prefix right before the
# harness launches. at-wine.sh `layout` only links the shim once it has been
# built, and on a BRAND-NEW prefix the first `setup` runs wineboot + the heavy
# registry import which repopulate system32/msctf.dll with Wine's real builtin —
# clobbering (or pre-empting) that symlink. The result is the documented first-
# run failure: 0 MsctfShim lines, ActivateLanguageProfile 0x80070057, keys not
# eaten, romaji passthrough — only the *second* run worked. Forcing the link
# here, after all wineboot/registry work and before the TIP loads msctf, makes
# the first run convert too. (No-op until build-msctf-shim.sh has produced the
# shim; harmless to re-run.)
ensure_msctf_shim_linked() {
  local shim="$PWD/native/MsctfShim/msctf.dll"
  local helper="$PWD/native/MsctfShim/msctf-helper.dll"
  local d
  [[ -f "$shim" ]] || return 0
  for d in "$PREFIX/drive_c/windows/system32" "$PREFIX/drive_c/windows/syswow64"; do
    [[ -d "$d" ]] || continue
    ln -sfn "$shim" "$d/msctf.dll"
    [[ -f "$helper" ]] && ln -sfn "$helper" "$d/msctf-helper.dll"
  done
}

# Build everything the warm-loop workflow touches (runtime components + the
# AtCtl client), without launching anything. Use once after editing source,
# then drive the resident runtime with `ctl`/`drive` (no further rebuilds).
build_all() {
  build_runtime_components
  bash "$HERE/build-at-ctl.sh"
}

run_tipload() {
  "$WRAPPER" setup
  kill_existing_tipruntime
  build_runtime_components
  prepare_orchestrated_servers
  ensure_msctf_shim_linked
  run_wine "$PWD/native/AtTipLoad/AtTipLoad.exe"
}

run_tipload_runtime() {
  "$WRAPPER" setup
  kill_existing_tipruntime
  build_runtime_components
  prepare_orchestrated_servers
  ensure_msctf_shim_linked
  AT_TIPLOAD_RUNTIME=1 run_wine "$PWD/native/AtTipLoad/AtTipLoad.exe"
}

# Relay (../atzc-server) support. ATOK's reconversion edit-session teardown
# faults on a long-lived process (a known Wine bug), so the relay uses a fresh
# single-shot TIP per conversion against a warm server constellation:
#
#   daemon-up    one-time heavy bring-up — prefix + native build + warm
#                ATOK31OM/DV/IB servers. Leaves the servers running; does NOT
#                start the TIP. All output goes to stderr. Run once.
#   daemon-once  one conversion: a fresh AT_TIPLOAD_DAEMON harness against the
#                already-warm servers (no rebuild, no server restart). Speaks the
#                ATD line protocol on stdin/stdout; exits (or faults in teardown)
#                after the conversion. Run per request.
run_daemon_up() {
  {
    "$WRAPPER" setup
    kill_existing_tipruntime
    build_runtime_components
    prepare_orchestrated_servers
    ensure_msctf_shim_linked
  } 1>&2
}
run_daemon_once() {
  ensure_layout 1>&2
  ensure_msctf_shim_linked 1>&2
  AT_TIPLOAD_DAEMON=1 AT_SKIP_BUILD=1 run_wine "$PWD/native/AtTipLoad/AtTipLoad.exe"
}

# Send one or more `;`-separated runtime commands to the resident tipruntime and
# print the tipruntime.log lines they produced. This is the fast inner loop:
# launch `tipruntime` once (warm servers + activated profile), then iterate with
# `drive "reset; type kisha; reading; reconv"` — no rebuild, no server restart.
# Override the settle time with AT_DRIVE_WAIT (seconds, default 2.5).
drive_runtime() {
  ensure_layout
  bash "$HERE/build-at-ctl.sh" >/dev/null 2>&1 || true
  local log="$PWD/tipruntime.log"
  local before=0
  [[ -f "$log" ]] && before=$(wc -c < "$log" | tr -d ' ')
  run_wine "$PWD/native/AtCtl/AtCtl.exe" tip "$*" || true
  sleep "${AT_DRIVE_WAIT:-2.5}"
  if [[ -f "$log" ]]; then
    echo "----- tipruntime.log (+$(( $(wc -c < "$log" | tr -d ' ') - before )) bytes) -----"
    tail -c "+$((before + 1))" "$log"
  fi
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
    "$WRAPPER" setup
    kill_existing_tipruntime
    build_runtime_components
    prepare_orchestrated_servers
    ensure_msctf_shim_linked
    AT_SKIP_RT_PRELOAD=${AT_SKIP_RT_PRELOAD:-1} \
    AT_SKIP_EXPLICIT_TIP=${AT_SKIP_EXPLICIT_TIP:-0} AT_STICKY_KEYSINK=1 \
      AT_TIPLOAD_RUNTIME=1 AT_TIPLOAD_SNAPSHOT_ONLY=1 \
      run_wine "$PWD/native/AtTipLoad/AtTipLoad.exe"
  } >"$run_out" 2>&1; then
    :
  else
    rc=$?
  fi
  cleanup_snapshot
  trap - EXIT
  return "$rc"
}

run_shmdump() {
  ensure_layout
  bash "$HERE/build-at-shmdump.sh"
  run_wine "$PWD/native/AtShmDump/AtShmDump64.exe" "$@"
}

run_target() {
  ensure_layout
  kill_existing_target
  bash "$HERE/build-at-target.sh"
  run_wine "$PWD/native/AtTarget/AtTarget.exe" "$@"
}

run_ctl() {
  ensure_layout
  bash "$HERE/build-at-ctl.sh"
  run_wine "$PWD/native/AtCtl/AtCtl.exe" "$@"
}

send_msg() {
  ensure_layout
  bash "$HERE/build-at-msg.sh"
  run_wine "$PWD/native/AtMsg/AtMsg.exe" "${1:-IncrementTip}"
}

trace_msg() {
  ensure_layout
  bash "$HERE/build-at-msg.sh"
  local timeout_s=${AT_TRACE_TIMEOUT:-12s}
  timeout "$timeout_s" \
    env WINEPREFIX="$PREFIX" WINEDEBUG=+server,+msg,+win,+loaddll "$WINE_BIN" "$PWD/native/AtMsg/AtMsg.exe" "$@" 2>&1 |
    rg -i 'send_inter_thread_message|retrieve_reply|RegisterWindowMessageW|AtMsg arg0|AtMsg sent|AtMsg wp|AtMsg lp|AtMsg ret|trace:msg|trace:server|ATOK|msg c0'
}

trace_target_app() {
  ensure_layout
  kill_existing_target
  bash "$HERE/build-at-target.sh"
  local timeout_s=${AT_TRACE_TIMEOUT:-10s}
  timeout "$timeout_s" \
    env WINEPREFIX="$PREFIX" WINEDEBUG=+server,+msg,+win,+loaddll "$WINE_BIN" "$PWD/native/AtTarget/AtTarget.exe" "$@" 2>&1 |
    rg -i 'create_(mutex|event|named_pipe|file_mapping|private_namespace)|open_(mutex|event|named_pipe|file_mapping|private_namespace)|create_window|register_class|find_window|send_message|post_message|peek_message|get_message|setwindowtheme|setwindowlong|setwindowpos|ATOK|JSFLT|ATFSVR|msctf|tip|LangBarUI|InputConvert|Reconversion|SearchCandidateProvider|DocumentFeed|Composition|WM_IME|WM_KEY'
}

register_tip() {
  ensure_layout
  run_wine regsvr32.exe /s "Z:$(resolve_bundle_file ATOK31TIP.DLL)"
}

scan_tip_registry() {
  local reg_dump

  ensure_layout
  run_wine regsvr32.exe /s "Z:${TIP_DLL}"

  reg_dump=$(mktemp)
  trap 'rm -f "${reg_dump:-}"' RETURN

  {
    run_wine reg.exe query 'HKLM\Software\Classes\CLSID' /s
    run_wine reg.exe query 'HKLM\Software\Classes\Wow6432Node\CLSID' /s
    run_wine reg.exe query 'HKLM\Software\Microsoft\CTF' /s
    run_wine reg.exe query 'HKLM\Software\Microsoft\CTF\TIP' /s
  } >"$reg_dump" 2>&1 || true

  rg -i 'ATOK|TIP|TextService|msctf|InputConvert|SearchCandidateProvider|Reconversion|LangBarUI|Wow6432Node|1314EB53-CACA-4152-A556-A184143202AF' "$reg_dump" || true
}

trace_target() {
  exec "$WRAPPER" trace "$@"
}

main() {
  local cmd=${1:-}
  shift || true

  case "$cmd" in
    ""|run)
      if [[ $# -eq 0 ]]; then
        run_default
      fi
      exec "$WRAPPER" run "$@"
      ;;
    mn|main)
      run_main "$@"
      ;;
    fs|server)
      run_server "$@"
      ;;
    flt|filter)
      run_filter_regserver "$@"
      ;;
    nsshim)
      run_nsshim
      ;;
    probe)
      run_probe
      ;;
    fetch-wine)
      bash "$HERE/fetch-wine.sh"
      ;;
    build)
      build_all
      ;;
    tipload)
      run_tipload
      ;;
    tipruntime)
      run_tipload_runtime
      ;;
    daemon-up)
      run_daemon_up
      ;;
    daemon-once)
      run_daemon_once
      ;;
    drive)
      drive_runtime "$@"
      ;;
    tmex)
      if [[ $# -eq 0 ]]; then
        run_ctl tip tmex
      else
        run_ctl tip "$@" tmex
      fi
      ;;
    tmexsnap)
      if [[ $# -eq 0 ]]; then
        run_ctl tip tmexsnap
      else
        run_ctl tip "$1" tmexsnap
      fi
      ;;
    manualtipsnap)
      if [[ $# -eq 0 ]]; then
        run_ctl tip manualtipsnap
      else
        run_ctl tip "$1" manualtipsnap
      fi
      ;;
    tipsnapshot)
      run_tipload_snapshot
      ;;
    shmdump)
      run_shmdump "$@"
      ;;
    target)
      run_target "$@"
      ;;
    ctl)
      run_ctl "$@"
      ;;
    traceapp)
      trace_target_app "$@"
      ;;
    msg)
      send_msg "$@"
      ;;
    msgtrace)
      trace_msg "$@"
      ;;
    shell)
      exec "$WRAPPER" shell "$@"
      ;;
    tipreg)
      register_tip
      ;;
    scan)
      scan_tip_registry
      ;;
    trace)
      if [[ $# -eq 0 ]]; then
        trace_target "$DEFAULT_EXE"
      fi
      case "${1:-}" in
        mn|main)
          shift
          trace_target ATOK31MN.EXE "$@"
          ;;
        fs|server)
          shift
          trace_target ATFSVR31.EXE "$@"
          ;;
        flt|filter)
          shift
          trace_target JSFLT.exe Regserver "$@"
          ;;
        *)
          trace_target "$@"
          ;;
      esac
      ;;
    traceall)
      exec "$WRAPPER" traceall
      ;;
    traceibdv)
      exec "$WRAPPER" traceibdv
      ;;
    traceallcmp)
      exec "$WRAPPER" traceallcmp
      ;;
    traceallobj)
      exec "$WRAPPER" traceallobj
      ;;
    traceallnames)
      exec "$WRAPPER" traceallnames
      ;;
    tracealldelta)
      exec "$WRAPPER" tracealldelta
      ;;
    tracejsflt)
      exec "$WRAPPER" tracejsflt
      ;;
    tracejsfltboot)
      exec "$WRAPPER" tracejsfltboot
      ;;
    tracejsfltraw)
      exec "$WRAPPER" tracejsfltraw
      ;;
    tracejsfltdiff)
      exec "$WRAPPER" tracejsfltdiff
      ;;
    tracejsfltcmp)
      exec "$WRAPPER" tracejsfltcmp
      ;;
    ipc)
      if [[ $# -eq 0 ]]; then
        exec "$WRAPPER" traceipc "$DEFAULT_EXE"
      fi
      case "${1:-}" in
        mn|main)
          shift
          exec "$WRAPPER" traceipc ATOK31MN.EXE "$@"
          ;;
        fs|server)
          shift
          exec "$WRAPPER" traceipc ATFSVR31.EXE "$@"
          ;;
        flt|filter)
          shift
          exec "$WRAPPER" traceipc JSFLT.exe Regserver "$@"
          ;;
        all)
          shift
          exec "$WRAPPER" traceall "$@"
          ;;
        *)
          exec "$WRAPPER" traceipc "$@"
          ;;
      esac
      ;;
    setup)
      exec "$WRAPPER" setup
      ;;
    -h|--help|help)
      usage
      ;;
    *)
      exec "$WRAPPER" run "$cmd" "$@"
      ;;
  esac
}

main "$@"
