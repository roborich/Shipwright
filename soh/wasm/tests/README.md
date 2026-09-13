# SoH wasm tests

Browser tests for the Emscripten build, run with [bun](https://bun.sh) and Playwright.
They load the build the way an embedder does, hand it game files as bytes, and check what
comes back: the events and commands in `soh/wasm/HOST-API.md`, boot behaviour, and a few
properties of the built artifacts.

Nothing here runs in CI. The tests need a wasm build and an `oot.o2r` extracted from your
own ROM, and neither belongs in the repo.

## Setup

```sh
cd soh/wasm/tests
bun install
bun run install-browser   # once, fetches Playwright's Chromium
```

## Running

```sh
bun test unit/        # pure helpers, no build needed (seconds)
bun test smoke        # boots the game (a few minutes)
bun test              # everything that is enabled
```

| Variable | Default | Meaning |
|---|---|---|
| `SOH_WASM_BUILD` | `build-wasm-rel/soh` | Directory with `soh.js`, `soh.wasm`, `soh.data` |
| `SOH_WASM_FILES` | `$SOH_WASM_BUILD/hostfiles` | `oot.o2r`, `shipofharkinian.json`, `Save/file1.sav`, `Save/global.sav` |
| `SOH_WASM_SLOW` | unset | `1` runs the timing tests |
| `SOH_WASM_SOAK` | unset | `1` runs the 10-minute memory soak |
| `SOH_WASM_KNOWN_BUGS` | unset | `1` also runs tests for bugs that are not fixed yet |
| `SOH_WASM_HEADED` | unset | `1` shows the browser |

The files directory needs a desktop-style config whose keyboard mapping has A on X, B on C
and Start on Space; the tests drive file select with those keys.

## Layout

- `page/harness.html` stands in for an embedder: it fetches the files it is told to, writes
  them into the VFS before `main()`, and loads `soh.js`.
- `lib/game.ts` drives one page: event log, `run(command)`, VFS access, key presses, and a
  check that the canvas actually shows something.
- `unit/` tests the pure helpers under `lib/`.
- `knownBug(...)` marks a test for a bug that is not fixed yet. It is skipped unless
  `SOH_WASM_KNOWN_BUGS=1`; the commit that fixes the bug turns it into a plain `test`.
