#!/usr/bin/env bash
# at-provision.sh — turn a harvested bundle (manifest.json + files/ + registry/)
# into the version-agnostic inputs the Wine side needs, then optionally apply
# them to a prefix.
#
# This is the consumer of the harvester's manifest (see docs/at-bundle-manifest.md
# and scripts/windows/at-harvest.ps1). It exists so nothing on the Wine side
# hardcodes ATOK31* / 31.2.5 / ATOK31T29 / the TIP CLSID — those all come from
# the manifest. The version-agnostic work is *config injection*, done here.
#
# Stages (the first three need NO Wine and are always run):
#   1. parse manifest.json
#   2. render the registry template (fill ${AT_INSTALL_DIR} / ${AT_USER})
#   3. reconstruct an at-wine.sh-compatible install root from the bundle's
#      files/ layout, and emit provision.env (INSTALL_DIR, TIP CLSID, profile,
#      langid, IPC name templates with <USER> filled, AT_LOCAL_ROOT)
#   4. --apply: feed that env to `at-wine.sh setup` + import the registry +
#      run a smoke self-test. Requires Wine — run inside the dev container.
#
# Usage:
#   scripts/at-provision.sh <bundle-dir> [--prefix <wine-prefix>]
#                             [--user <wine-username>] [--apply] [--force]

set -euo pipefail

BUNDLE=""
PREFIX="${AT_WINEPREFIX:-$PWD/.wine-atok-local}"
WINE_USER="${AT_WINE_USER:-${USER:-wine}}"
APPLY=0
FORCE=0
# Where ATOK lands inside the Wine prefix (matches at-wine.sh WIN_INSTALL).
WIN_JUSTSYSTEMS_REL="drive_c/Program Files/JustSystems"

log()  { printf '%s [%-5s] %s\n' "$(date +%H:%M:%S)" "$1" "$2"; }
info() { log INFO  "$1"; }
warn() { log WARN  "$1" >&2; }
err()  { log ERROR "$1" >&2; }
die()  { err "$1"; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix) PREFIX="$2"; shift 2;;
    --user)   WINE_USER="$2"; shift 2;;
    --apply)  APPLY=1; shift;;
    --force)  FORCE=1; shift;;
    -h|--help) grep '^#' "$0" | sed 's/^# \?//'; exit 0;;
    -*) die "unknown flag: $1";;
    *) [[ -z "$BUNDLE" ]] && BUNDLE="$1" || die "unexpected arg: $1"; shift;;
  esac
done

[[ -n "$BUNDLE" ]] || die "usage: at-provision.sh <bundle-dir> [--prefix P] [--user U] [--apply] [--force]"
MANIFEST="$BUNDLE/manifest.json"
[[ -f "$MANIFEST" ]] || die "no manifest.json in bundle: $BUNDLE"
command -v python3 >/dev/null || die "python3 is required to parse the manifest"

# --- 1. Parse manifest into shell variables (python emits shell-quoted assigns).
info "Reading $MANIFEST"
eval "$(python3 - "$MANIFEST" <<'PY'
import json, sys, shlex
# utf-8-sig tolerates a UTF-8 BOM (Windows PowerShell 5.1 writes one).
m = json.load(open(sys.argv[1], encoding="utf-8-sig"))
a, ipc, diag = m["atok"], m.get("ipc", {}), m.get("diagnostics", {})
names = ipc.get("names", {})
def put(k, v): print(f"{k}={shlex.quote('' if v is None else str(v))}")
put("M_PRODUCT", a.get("product_version")); put("M_MODULE", a.get("module_version"))
put("M_MAJOR", a.get("major")); put("M_LEAF", a.get("install_leaf"))
put("M_CLSID", a.get("tip_clsid")); put("M_TIPDLL", a.get("tip_dll"))
put("M_LANGID", a.get("langid")); put("M_PROFILE", a.get("langprofile_guid"))
put("M_VERDICT", diag.get("verdict", "unknown"))
put("M_INSTALL_ROOT_WIN", a.get("install_root_win"))
for k, v in names.items(): put(f"N_{k.upper()}", v)
PY
)"

[[ -n "${M_LEAF:-}" ]] || die "manifest missing atok.install_leaf"
info "ATOK product=$M_PRODUCT module=$M_MODULE major=$M_MAJOR leaf=$M_LEAF clsid=$M_CLSID verdict=$M_VERDICT"

# --- verdict gate.
if [[ "$M_VERDICT" == "will-not-provision" ]]; then
  warn "manifest verdict is 'will-not-provision' — the bundle is missing blocking pieces (engine DLL or TIP CLSID). See $BUNDLE/harvest.log."
  [[ $APPLY -eq 1 && $FORCE -ne 1 ]] && die "refusing to --apply a will-not-provision bundle (override with --force)"
fi

PROV="$BUNDLE/provision"
mkdir -p "$PROV"

# --- 2. Render the registry templates (HKLM + HKCU; fill placeholders).
WIN_INSTALL_TARGET="C:\\Program Files\\JustSystems\\$M_LEAF"
render_reg() {  # <hive>  -> $PROV/atok-<hive>.reg
  local hive="$1" tpl="$BUNDLE/registry/atok-$1.reg.template" out="$PROV/atok-$1.reg"
  [[ -f "$tpl" ]] || return 1
  python3 - "$tpl" "$out" "$WIN_INSTALL_TARGET" "$WINE_USER" <<'PY'
import sys
src, dst, install, user = sys.argv[1:5]
data = open(src, encoding="utf-16" if open(src,'rb').read(2)==b'\xff\xfe' else "utf-8-sig").read()
# .reg values escape backslashes as \\ — escape the install path before substituting.
data = data.replace("${AT_INSTALL_DIR}", install.replace("\\", "\\\\")).replace("${AT_USER}", user)
open(dst, "w", encoding="utf-8").write(data)
PY
  info "Rendered registry -> $out"
}
REG_OUT="$PROV/atok-hklm.reg"
render_reg hklm || warn "no HKLM registry template in bundle; skipping"
render_reg hkcu || warn "no HKCU registry template in bundle (engine config may be incomplete)"

# --- 3. Reconstruct an at-wine.sh-compatible install root from files/.
#        at-wine.sh resolve_bundle_file expects <root>/<leaf>/, <root>/SysWOW64/,
#        <root>/<leaf>_X64/. Symlink the bundle's files/ into that shape.
ROOT="$PROV/root"
mkdir -p "$ROOT"
link() { [[ -e "$2" ]] && ln -sfn "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")" "$2" || true; }
PROG="$BUNDLE/files/program/$M_LEAF"
[[ -d "$PROG" ]] && ln -sfn "$(cd "$PROG" && pwd)" "$ROOT/$M_LEAF" && info "  root/$M_LEAF -> files/program/$M_LEAF"
[[ -d "$BUNDLE/files/program/${M_LEAF}_X64" ]] && ln -sfn "$(cd "$BUNDLE/files/program/${M_LEAF}_X64" && pwd)" "$ROOT/${M_LEAF}_X64"
# 32-bit engine: prefer SysWOW64, else System32 — at-wine.sh looks in <root>/SysWOW64.
for sysdir in SysWOW64 System32; do
  if [[ -d "$BUNDLE/files/system/$sysdir" ]]; then
    ln -sfn "$(cd "$BUNDLE/files/system/$sysdir" && pwd)" "$ROOT/SysWOW64"
    info "  root/SysWOW64 -> files/system/$sysdir"
    break
  fi
done
# The rendered regs, named where at-wine.sh's import looks for them.
[[ -f "$PROV/atok-hklm.reg" ]] && ln -sfn "$(cd "$PROV" && pwd)/atok-hklm.reg" "$ROOT/hklm.reg"
[[ -f "$PROV/atok-hkcu.reg" ]] && ln -sfn "$(cd "$PROV" && pwd)/atok-hkcu.reg" "$ROOT/hkcu.reg"

# --- emit provision.env (sourced by at-wine.sh via its override env vars).
fill_user() { printf '%s' "${1//<USER>/$WINE_USER}"; }
ENV_OUT="$PROV/provision.env"
{
  echo "# Generated by at-provision.sh from $MANIFEST"
  echo "# Source this, then run scripts/at-wine.sh setup (or use --apply)."
  echo "export AT_LOCAL_ROOT=$(printf '%q' "$(cd "$ROOT" && pwd)")"
  echo "export AT_HOST_ROOT=$(printf '%q' "$(cd "$ROOT" && pwd)")"
  echo "export AT_INSTALL_DIR=$(printf '%q' "$M_LEAF")"
  echo "export AT_TIP_CLSID=$(printf '%q' "$M_CLSID")"
  [[ -n "${M_PROFILE:-}" ]] && echo "export AT_PROFILE_GUID=$(printf '%q' "$M_PROFILE")"
  [[ -n "${M_LANGID:-}"  ]] && echo "export AT_LANGID=$(printf '%q' "$M_LANGID")"
  echo "export AT_MAJOR=$(printf '%q' "$M_MAJOR")"
  echo "export AT_MODULE=$(printf '%q' "$M_MODULE")"
  echo "export AT_WINE_USER=$(printf '%q' "$WINE_USER")"
  echo "export AT_REG_HKLM=$(printf '%q' "$REG_OUT")"
  # Resolved IPC object names (<USER> filled). Native runtime should read these
  # instead of compiling in ATOK31*/31.2.5.
  for v in CE_SHARED_DATA SHARED_MEMORY COMMON_MEMORY TIMEOUT_COUNT DN2_MODEL DN4_MODEL OM_APP_MUTEX ROMATABLE_MUTEX MRP_DB MRP_DB10 DIC_MUTEX; do
    src="N_$v"; val="${!src:-}"
    [[ -n "$val" ]] && echo "export AT_IPC_$v=$(printf '%q' "$(fill_user "$val")")"
  done
} > "$ENV_OUT"
info "Wrote $ENV_OUT"

echo
info "Provisioning inputs ready under $PROV/"
echo "  env:      $ENV_OUT"
echo "  registry: $REG_OUT"
echo "  root:     $ROOT  (AT_LOCAL_ROOT)"
echo "  IPC CE name (resolved): $(fill_user "${N_CE_SHARED_DATA:-<none>}")"

# --- 4. Apply (Wine; dev container).
if [[ $APPLY -eq 1 ]]; then
  if ! command -v "${AT_WINE:-wine}" >/dev/null; then
    warn "--apply requested but Wine not found. Run inside the dev container (scripts/at-docker.sh)."
    exit 3
  fi
  info "Applying to prefix $PREFIX via at-wine.sh setup"
  # shellcheck disable=SC1090
  set -a; source "$ENV_OUT"; set +a
  export AT_WINEPREFIX="$PREFIX"
  "$(dirname "$0")/at-wine.sh" setup
  info "Applied. Run a smoke test: scripts/at.sh tipload  (expect 日本語)."
else
  echo
  info "Next: source the env + apply, or run with --apply (needs Wine / dev container):"
  echo "    source $ENV_OUT && scripts/at-wine.sh setup"
fi
