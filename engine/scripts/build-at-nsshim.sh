#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/.." && pwd)
SRC_DIR="$ROOT/native/AtNsShim"
BUILD_DIR="$SRC_DIR/build"
OUT="$SRC_DIR/AtNsShim.dll"

mkdir -p "$BUILD_DIR"

llvm-dlltool -m i386 -d "$SRC_DIR/kernel32.def" -l "$BUILD_DIR/libkernel32.a"

clang --target=i686-w64-windows-gnu -shared -ffreestanding -fno-stack-protector -nostdlib -fuse-ld=lld \
  -isystem /usr/include \
  -isystem /usr/include/wine/windows \
  -Wl,--export-all-symbols \
  -Wl,--subsystem,windows -Wl,--entry,DllMain \
  -o "$OUT" "$SRC_DIR/AtNsShim.c" \
  -L"$BUILD_DIR" -lkernel32
