# atzc — headless Japanese conversion relay

Drives a Wine-hosted Japanese conversion engine headless and exposes it to
native Linux input methods and other consumers over a small Unix-socket protocol.

```
 fcitx5 addon ┐                              atzc-server
 ibus engine  ┼─ unix socket ─→ atzcd ──────→ engine/  (bundled harness)
 modore       ┘   (libatzcclient)             scripts/ + native/  → engine (Wine, headless)
                                              + your engine install & Wine (.atzc/)
```

- **`atzcd`** — the daemon. Owns the engine, serves conversion requests.
- **`engine/`** — the bundled Wine harness: `native/` (the TSF driver + a small
  msctf shim) + `scripts/` (`at.sh daemon-up/daemon-once`).
- **`libatzcclient`** — the C++ client + `atzc::MakeConverter`; shared by the
  fcitx5 addon and (out of tree) by modore's Linux backend.
- **`atzc`** — a shell test client. **`fcitx5/`** — the addon (built when fcitx5
  dev packages exist).

The wire protocol is one TAB-separated line per request/reply — see
[docs/protocol.md](docs/protocol.md).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j        # -> build/{atzcd,atzc,libatzcclient} (+ fcitx5 addon if present)
```

That builds the C++ daemon/client (C++17). The `engine/` harness is a Windows-PE
toolchain cross-compiled on demand the first time `atzcd` runs `daemon-up`, so
that host also needs `clang` + `lld` + `llvm-dlltool` and the Wine SDK
headers/libs. The fcitx5 addon additionally needs `Fcitx5Core`/`Fcitx5Utils`.

## Bring your own

The engine is a third-party, proprietary Japanese IME — **not included**. Supply a
licensed install you harvested from a Windows machine:

- **Engine install** → `engine/.atzc/at/` (or set `AT_HOME`): the IME's TSF text
  service (TIP) DLL, its conversion-server executables, the registry exports, and
  the engine DLLs. The launcher resolves them by their standard install names.
- **Wine** → `engine/.atzc/wine/` via `engine/scripts/fetch-wine.sh`, or system Wine.
- **Registry captures** (needed for the *full* candidate list, not just top-1) →
  `engine/recon/captures/`, exported from the same Windows machine.

None of the above is committed; it stays under `.atzc/` and `recon/captures/`
(both git-ignored).

## Run

```bash
build/atzcd                       # uses the bundled engine/; brings the engine up, then serves
# elsewhere:
build/atzc convert kisha          # fast top-1   -> commit 貴社
build/atzc candidates kisha       # full list    -> 貴社 記者 汽車 …
```

On start `atzcd` warms the engine once (~40 s warm, a few minutes cold), then
keeps one warm instance serving every request. Options: `--engine-dir <path>`
(override the bundled harness), `--socket <path>`, `--type-delay-ms <n>`.
