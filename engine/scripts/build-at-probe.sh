#!/usr/bin/env bash
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/.." && pwd)
SRC_DIR="$ROOT/native/AtProbe"
BUILD_DIR="$SRC_DIR/build"
OUT="$SRC_DIR/AtProbe.exe"

mkdir -p "$BUILD_DIR"

llvm-dlltool -m i386:x86-64 -d "$SRC_DIR/user32.def" -l "$BUILD_DIR/libuser32.a"
llvm-dlltool -m i386:x86-64 -d "$SRC_DIR/kernel32.def" -l "$BUILD_DIR/libkernel32.a"
llvm-dlltool -m i386:x86-64 -d "$SRC_DIR/imm32.def" -l "$BUILD_DIR/libimm32.a"

clang --target=x86_64-w64-windows-gnu -ffreestanding -fno-stack-protector -nostdlib -fuse-ld=lld \
  -Wl,--subsystem,console -Wl,--entry,mainCRTStartup \
  -o "$OUT" "$SRC_DIR/AtProbe.c" \
  -L"$BUILD_DIR" -luser32 -lkernel32 -limm32
