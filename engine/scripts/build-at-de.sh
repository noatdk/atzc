#!/usr/bin/env bash
set -euo pipefail

# Builds the Atok31De.dll loader stub. ATOK31TIP.DLL imports ordinals 5-9 from
# Atok31De.dll; this stub satisfies the loader so ATOK31TIP.DLL can be loaded
# and COM-registered. Exports are by ordinal (NONAME) per Atok31De.def.
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/.." && pwd)
SRC_DIR="$ROOT/native/Atok31De"
OUT="$SRC_DIR/Atok31De.dll"

clang --target=i686-w64-windows-gnu -shared -ffreestanding -fno-stack-protector -nostdlib -fuse-ld=lld \
  -Wl,--kill-at \
  -Wl,--subsystem,windows -Wl,--entry,DllMain \
  -o "$OUT" "$SRC_DIR/Atok31De.c" "$SRC_DIR/Atok31De.def"
