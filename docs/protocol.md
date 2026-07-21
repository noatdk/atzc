# atzcd socket protocol

`atzcd` listens on a Unix-domain stream socket (default `$XDG_RUNTIME_DIR/atzcd.sock`).
One request per line, one reply per line. UTF-8, fields separated by TAB, terminated
by LF. Japanese text contains neither, so no escaping is needed. A connection may be
reused; `atzcd` serves one at a time (the engine is a singleton).

Two request families share the framing, and may be mixed on one connection:

- **Batch** (stateless): a request carries a whole romaji reading and gets a fresh
  conversion; the candidate list is returned whole, selection is client-side. Use for
  the inline top-1 plus a candidate popup.
- **Session** (stateful): one long-lived composition on the daemon. The client streams
  keystrokes/edits/henkan and reads back the live preedit, then commits. Use for an
  input method that tracks the composition keystroke-by-keystroke.

## Batch

| Request | Meaning |
| --- | --- |
| `convert\t<romaji>` | fast top-1 only; also primes the candidate prefetch |
| `candidates\t<romaji>[\t<max>]` | top-1 + all candidates (flat surfaces), capped to `<max>` |
| `candidates-ex\t<romaji>[\t<max>]` | enriched list: reading + 品詞 per candidate |
| `ping` | liveness check |

| Reply | Meaning |
| --- | --- |
| `ok\t<commit>[\t<cand1>\t<cand2>…]` | success; `commit` is top-1 (`== cand1` when candidates present) |
| `okx\t<commit>\t<surf1>\t<read1>\t<hinsi1>\t<surf2>…` | enriched reply to `candidates-ex` |
| `err\t<message>` | failure |
| `pong` | reply to `ping` |

`<romaji>` is ASCII keystrokes as typed (`kisha`, `nihongo`). `okx` appends three TAB
fields per candidate after `commit`: `surface`, `reading` (hiragana), `hinsi` (POS label
e.g. `名詞`, or hex like `0x08`). The enriched list is cleaner (no user-dict pollution)
and covers readings the flat list misses; with no enriched data the daemon emits flat
surfaces with empty reading/hinsi. The flat `candidates` reply is unchanged.

```
> convert	gakkou
< ok	学校
> candidates	gakkou
< ok	学校	学校	がっこう
> candidates-ex	koushou
< okx	交渉	交渉	こうしょう	名詞	校章	こうしょう	名詞	…
```

Call `convert` per keystroke; call `candidates`/`candidates-ex` only when the candidate
window opens. `convert` prefetches the list in the background, so the next candidate call
is usually a cache hit that leads with the same top-1. Selection is client-side (no
server-side "select index N").

## Session

One composition on the daemon's keepalive engine; each command mutates the live preedit
and echoes it back.

| Request | Meaning | Reply |
| --- | --- | --- |
| `session-begin` | start (or reset) a composition | `ok` (empty preedit) |
| `key\t<ascii>` | append romaji keystrokes, settle | `ok\t<kana>` |
| `backspace` | delete one kana | `ok\t<kana>` |
| `convert-session` | henkan in place; composition stays active | `ok\t<kanji>` |
| `preedit` | read the current preedit unchanged | `ok\t<text>` |
| `commit` | finalize the composition | `commit\t<text>` |
| `cancel` | abandon the composition | `ok` |
| `session-candidates\t<romaji>[\t<max>]` | full candidate list for a reading | `ok\t<commit>\t<cand>…` |

- `<ascii>` is keystrokes to append (usually one char; a run is accepted). `<kana>`/
  `<kanji>`/`<text>` is the resulting preedit.
- `commit` replies with the `commit` verb (not `ok`) so a finalize is distinguishable from
  an edit; the composition ends (next `session-begin` starts fresh).
- The daemon maps these onto the engine's session commands
  (`sreset`/`stype`/`sback`/`sconv`/`spre`/`scommit`), respawning the keepalive harness on
  a dead pipe — a respawn drops the composition, so the client re-`session-begin`s.
- `session-candidates` runs on the single-shot batch path, not the live session (a
  deliberate split: the heavy reconversion path stays single-shot). Selection is client-side.

```
> session-begin
< ok
> key	k          (…i, s, h, a)
< ok	k          (…きしゃ)
> convert-session
< ok	記者
> commit
< commit	記者
```

For the candidate window, call `session-candidates` with the accumulated reading (the
`<romaji>` fed via `key`), commit the chosen string client-side, and `cancel` the live
composition to keep the daemon in sync.

## Errors

Errors are non-fatal: a failed request returns `err`, the connection stays usable, and the
engine self-respawns on a backend crash. A session error (e.g. after a respawn) means the
composition was dropped — begin a new one.
