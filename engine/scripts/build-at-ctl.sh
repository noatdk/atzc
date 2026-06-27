#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/.." && pwd)
SRC_DIR="$ROOT/native/AtCtl"
BUILD_DIR="$SRC_DIR/build"
OUT="$SRC_DIR/AtCtl.exe"

mkdir -p "$BUILD_DIR"

llvm-dlltool -m i386 -d "$SRC_DIR/user32.def" -l "$BUILD_DIR/libuser32.a"
llvm-dlltool -m i386 -d "$SRC_DIR/kernel32.def" -l "$BUILD_DIR/libkernel32.a"

clang --target=i686-w64-windows-gnu -ffreestanding -fno-stack-protector -nostdlib -fuse-ld=lld \
  -fno-builtin-memcpy \
  -isystem /usr/include \
  -isystem /usr/include/wine/windows \
  -Wl,--subsystem,console -Wl,--entry,mainCRTStartup \
  -o "$OUT" "$SRC_DIR/AtCtl.c" \
  -L"$BUILD_DIR" -luser32 -lkernel32
