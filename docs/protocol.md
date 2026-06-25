# atzcd socket protocol

`atzcd` listens on a Unix-domain stream socket (default `$XDG_RUNTIME_DIR/atzcd.sock`).
Clients send one request per line and read one reply per line. UTF-8, fields
separated by TAB (`\t`), terminated by LF (`\n`). Japanese text never contains
TAB or LF, so no escaping is needed.

A connection may be reused for many requests. `atzcd` serves one connection at a
time (the engine is a singleton).

## Requests

| Line | Meaning |
| --- | --- |
| `convert\t<romaji>` | fast top-1 only; also primes the candidate prefetch |
| `candidates\t<romaji>` | full top-1 + all candidates |
| `candidates\t<romaji>\t<max>` | as above, capped to `<max>` candidates |
| `ping` | liveness check |

`<romaji>` is ASCII keystrokes as typed (e.g. `kisha`, `nihongo`).

## Replies

| Line | Meaning |
| --- | --- |
| `ok\t<commit>\t<cand1>\t<cand2>…` | success; `commit` is top-1, `commit == cand1` |
| `ok\t<commit>` | success with no extra candidates (typical `convert` reply) |
| `err\t<message>` | failure |
| `pong` | reply to `ping` |

## Example

```
> convert	gakkou
< ok	学校                                     (fast: ~0.08 s)
> candidates	gakkou
< ok	学校	学校	がっこう                  (instant: prefetched while you read the top-1)
```

## Usage

Call `convert` per keystroke for the inline top-1; call `candidates` only when the
user opens the candidate window. A `convert` also prefetches the candidate list in
the background, so the following `candidates` is usually a cache hit, and the list
leads with the same top-1. Candidate selection is client-side — there is no
server-side "select index N".

Errors are reported, never fatal: a failed request returns `err`, the connection
stays usable, and the engine self-respawns on a backend crash.
