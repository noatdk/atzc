# atzc-server — Linux IME relay backend

A relay daemon that exposes the Wine-hosted Japanese conversion engine to
native Linux input methods and other consumers over a small Unix-socket protocol.

```
 fcitx5 addon ┐
 ibus engine  ┼─ unix socket ─→ atzcd ─ stdin/stdout ─→ IME engine (Wine, headless)
 modore       ┘   (libatzcclient)
```

- **`atzcd`** — the daemon. Owns the engine, serves conversion requests.
- **`libatzcclient`** — the C++ client; shared by the fcitx5 addon and by modore's
  `AtzcBackend` (zero FFI — it implements modore's C++ `Backend` directly).
- **`atzc`** — a shell test client.
- **`fcitx5/`** — the input-method addon (built when fcitx5 dev packages exist).

The wire protocol is one TAB-separated line per request/reply — see
[docs/protocol.md](docs/protocol.md).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# -> build/atzcd, build/atzc, build/atzcclient (+ fcitx5 addon if fcitx5 is present)
```

Only a C++17 compiler is needed for `atzcd`/`atzc`/`libatzcclient`. The fcitx5
addon additionally needs `Fcitx5Core`/`Fcitx5Utils` dev packages.

## Run

Point `atzcd` at the engine directory (provisioned separately) with `--engine-dir`:

```bash
build/atzcd --engine-dir /path/to/engine        # brings the engine up, then serves
# elsewhere:
build/atzc convert kisha          # fast top-1
#   commit  貴社
build/atzc candidates kisha       # full list (usually prefetched)
#   commit  貴社
#   cand[0] 貴社
#   cand[1] 記者
#   ...
```

On start `atzcd` warms the engine once (~40 s warm, a few minutes cold), then keeps
one warm instance serving every request. Options: `--socket <path>`,
`--type-delay-ms <n>`.

