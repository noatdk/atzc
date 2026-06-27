#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/.." && pwd)
SRC_DIR="$ROOT/native/AtTipLoad"
BUILD_DIR="$SRC_DIR/build"
OUT="$SRC_DIR/AtTipLoad.exe"

# Build against the pinned Wine's SDK headers if present, else the system Wine's.
AT_HOME=${AT_HOME:-"$ROOT/.atzc"}
AT_WINE_PIN=""
source "$HERE/wine-pin.sh"
at_resolve_wine_pin "$AT_HOME/wine"
WINE_INC="${AT_WINE_PIN:+$AT_WINE_PIN/usr}"; WINE_INC="${WINE_INC:-/usr}/include/wine/windows"

mkdir -p "$BUILD_DIR"

llvm-dlltool -m i386 -d "$SRC_DIR/kernel32.def" -l "$BUILD_DIR/libkernel32.a"
llvm-dlltool -m i386 -d "$SRC_DIR/ole32.def" -l "$BUILD_DIR/libole32.a"
llvm-dlltool -m i386 -d "$SRC_DIR/msctf.def" -l "$BUILD_DIR/libmsctf.a"
llvm-dlltool -m i386 -d "$SRC_DIR/user32.def" -l "$BUILD_DIR/libuser32.a"
llvm-dlltool -m i386 -d "$SRC_DIR/imm32.def" -l "$BUILD_DIR/libimm32.a"
llvm-dlltool -m i386 -d "$SRC_DIR/advapi32.def" -l "$BUILD_DIR/libadvapi32.a"
llvm-dlltool -m i386 -d "$SRC_DIR/ntdll.def" -l "$BUILD_DIR/libntdll.a"

clang --target=i686-w64-windows-gnu -ffreestanding -fno-stack-protector -nostdlib -fuse-ld=lld \
  -isystem /usr/include \
  -isystem "$WINE_INC" \
  -Wl,--subsystem,console -Wl,--entry,mainCRTStartup \
  -o "$OUT" "$SRC_DIR/AtTipLoad.c" "$SRC_DIR/candidates.c" "$SRC_DIR/ce_probe.c" "$SRC_DIR/textstore.c" "$SRC_DIR/guids.c" "$SRC_DIR/crtshim.c" \
  -L"$BUILD_DIR" -lmsctf -lole32 -ladvapi32 -luser32 -limm32 -lkernel32 -lntdll
