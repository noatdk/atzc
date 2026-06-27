#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/.." && pwd)
SRC_DIR="$ROOT/native/MsctfShim"
BUILD_DIR="$SRC_DIR/build"
OUT="$SRC_DIR/msctf.dll"
HELPER_OUT="$SRC_DIR/msctf-helper.dll"

# Build against the pinned Wine if present (headers + the matching builtin msctf
# the shim forwards to), else the system Wine. Keeping the helper and headers from
# the same Wine the harness runs under is what makes the shim version-consistent.
AT_HOME=${AT_HOME:-"$ROOT/.atzc"}
AT_WINE_PIN=""
source "$HERE/wine-pin.sh"
at_resolve_wine_pin "$AT_HOME/wine"
WINE_USR="${AT_WINE_PIN:+$AT_WINE_PIN/usr}"; WINE_USR="${WINE_USR:-/usr}"
WINE_INC="$WINE_USR/include/wine/windows"
HELPER_SRC="$WINE_USR/lib/wine/i386-windows/msctf.dll"

mkdir -p "$BUILD_DIR"
cp -f "$HELPER_SRC" "$HELPER_OUT"

llvm-dlltool -m i386 -d "$SRC_DIR/msctf.def" -l "$BUILD_DIR/libmsctf.a"
llvm-dlltool -m i386 -d "$ROOT/native/AtTipLoad/kernel32.def" -l "$BUILD_DIR/libkernel32.a"
llvm-dlltool -m i386 -d "$ROOT/native/AtTipLoad/ole32.def" -l "$BUILD_DIR/libole32.a"
llvm-dlltool -m i386 -d "$ROOT/native/AtTipLoad/user32.def" -l "$BUILD_DIR/libuser32.a"

clang --target=i686-w64-windows-gnu -shared -ffreestanding -fno-stack-protector -nostdlib -fuse-ld=lld \
  -isystem /usr/include \
  -isystem "$WINE_INC" \
  -Wl,--export-all-symbols \
  -Wl,--kill-at \
  -Wl,--subsystem,windows -Wl,--entry,DllMain \
  -o "$OUT" "$SRC_DIR/MsctfShim.c" "$SRC_DIR/crtshim.c" "$SRC_DIR/guids.c" \
  -L"$BUILD_DIR" -lmsctf -lkernel32 -lole32 -luser32
