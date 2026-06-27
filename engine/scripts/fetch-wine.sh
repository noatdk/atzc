#!/usr/bin/env bash
# Fetch the pinned Wine build (see ../wine.lock) into $AT_HOME/wine — no root, no
# vendored binaries. Downloads the exact package, verifies its sha256, extracts
# it, and writes relocatable wine/wineserver wrappers. Idempotent: a verified
# extract is reused; AT_WINE_FORCE=1 re-fetches.
#
# $AT_HOME (default .atzc) holds the external deps: wine/ (here) and at/ (the AT
# install). After this, the build scripts and at-wine.sh auto-detect
# $AT_HOME/wine/current and use it for the SDK headers, the builtin msctf, and the
# runtime loader (see scripts/wine-pin.sh); without it they use the system Wine.
set -euo pipefail

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/.." && pwd)
AT_HOME=${AT_HOME:-"$ROOT/.atzc"}
LOCK="$ROOT/wine.lock"
PIN_DIR="$AT_HOME/wine"

[[ -f "$LOCK" ]] || { echo "fetch-wine: missing $LOCK" >&2; exit 1; }
# shellcheck disable=SC1090
source "$LOCK"
: "${WINE_PKG_VERSION:?}" "${WINE_PKG_URL:?}" "${WINE_PKG_SHA256:?}"

dest="$PIN_DIR/$WINE_PKG_VERSION"
stamp="$dest/.ok"

if [[ -f "$stamp" && "${AT_WINE_FORCE:-0}" != 1 ]]; then
  echo "fetch-wine: $WINE_PKG_VERSION already present ($dest)"
else
  echo "fetch-wine: downloading wine $WINE_PKG_VERSION"
  rm -rf "$dest"
  mkdir -p "$dest"
  pkg="$dest/pkg.tar.zst"
  curl -fSL -o "$pkg" "$WINE_PKG_URL"

  got=$( { sha256sum "$pkg" 2>/dev/null || shasum -a 256 "$pkg"; } | awk '{print $1}')
  if [[ "$got" != "$WINE_PKG_SHA256" ]]; then
    echo "fetch-wine: SHA256 MISMATCH" >&2
    echo "  expected $WINE_PKG_SHA256" >&2
    echo "  got      $got" >&2
    rm -rf "$dest"
    exit 1
  fi
  echo "fetch-wine: sha256 ok; extracting"
  bsdtar -xf "$pkg" -C "$dest"
  rm -f "$pkg"

  mkdir -p "$dest/bin"
  for n in wine wineserver; do
    cat > "$dest/bin/$n" <<EOF
#!/bin/sh
# relocatable wrapper for the pinned Wine — resolves its own root from \$0.
R="\$(cd "\$(dirname "\$0")/.." && pwd)"
export LD_LIBRARY_PATH="\$R/usr/lib:\$R/usr/lib32\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export WINESERVER="\$R/usr/bin/wineserver"
export WINELOADER="\$R/usr/bin/wine"
exec "\$R/usr/bin/$n" "\$@"
EOF
    chmod +x "$dest/bin/$n"
  done
  touch "$stamp"
fi

ln -sfn "$WINE_PKG_VERSION" "$PIN_DIR/current"
echo "fetch-wine: pinned wine ready: $("$PIN_DIR/current/bin/wine" --version 2>/dev/null)"
echo "fetch-wine: root = $PIN_DIR/current"
