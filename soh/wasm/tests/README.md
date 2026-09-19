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

bun 1.4 or newer. 1.2.x drops Playwright's DevTools pipe partway through a multi-file run:
Chromium logs `Connection terminated while reading from pipe` and exits cleanly, and every
test after that waits out its full timeout on a browser that is gone. It never shows when a
single file runs, which is what made it look like the game. `run-tests.ts` refuses older
versions; `bun upgrade` fixes it.

## Playing a build

```sh
bun run serve         # http://127.0.0.1:8724/ -> host.html
```

Serves `SOH_WASM_BUILD` at `/` and `SOH_WASM_FILES` at `/hostfiles/`, which is where
`host.html` fetches `oot.o2r`, the config and saves. `PORT` changes the port, and `HOST=0.0.0.0`
makes it reachable from other devices. Nothing is cached, so a rebuild only needs a reload.

## Running the tests

```sh
bun test unit/        # pure helpers, no build needed (seconds)
bun test smoke        # boots the game (a few minutes)
bun test              # everything that is enabled
```

| Variable | Default | Meaning |
|---|---|---|
| `SOH_WASM_BUILD` | `build-wasm-rel/soh` | Directory with `soh.js`, `soh.wasm`, `soh.data` |
| `SOH_WASM_FILES` | `$SOH_WASM_BUILD/hostfiles` | `oot.o2r`, `shipofharkinian.json`, `Save/file1.sav`, `Save/global.sav`; optionally `oot-unbound.o2r` (a desktop build writes one next to its `oot.o2r`), which `unbound.test.ts` boots from alone |
| `SOH_WASM_SLOW` | unset | `1` runs the timing tests |
| `SOH_WASM_SOAK` | unset | `1` runs the 10-minute memory soak |
| `SOH_WASM_KNOWN_BUGS` | unset | `1` also runs tests for bugs that are not fixed yet |
| `SOH_WASM_HEADED` | unset | `1` shows the browser |
| `SOH_WASM_ROM` | unset | A ROM file; runs the ROM -> o2r converter tests (`extract.test.ts`) against it |

The files directory needs a desktop-style config whose keyboard mapping has A on X, B on C
and Start on Space; the tests drive file select with those keys.

`bun test` (via `run-tests.ts`) is capped at 30 minutes of wall clock, `SOH_WASM_BUDGET_MIN`
to change it; `bun test smoke` and `bun test unit/` run the runner directly.

## If a run hangs

`bun test` applies its timeout to test bodies only, not to `beforeAll` / `afterAll` hooks or
to its own exit. A page that never answers `page.evaluate` (bounded in `lib/game.ts`) also
never finishes closing, which is why `afterAll` bounds `browser.close()` and kills the
browser process if it does not finish, and why `run-tests.ts` caps the whole run. If a run
still sits idle, `ps -o pid,etime,command | grep "bun test"` finds it; one listening socket
and no browser under it means the runner finished its tests and could not exit.

To see what the browser itself is doing, run with `DEBUG=pw:browser`: Playwright then prints
every browser launch, exit code, and stderr line, which is how the bun pipe bug above was
found. A failed call into the page also appends the page's recent console errors to the
error, which arrive over the protocol and so survive a stuck main thread.

## Layout

- `page/harness.html` stands in for an embedder: it fetches the files it is told to, writes
  them into the VFS before `main()`, and loads `soh.js`.
- `lib/game.ts` drives one page: event log, `run(command)`, VFS access, key presses, and a
  check that the canvas actually shows something.
- `page/extract.html` + `page/extract-worker.js` stand in for a host converting a ROM in a
  module worker with `soh-extract.js`; `extract.test.ts` also runs the converter in-process
  and holds its archive to the desktop-extracted one in `SOH_WASM_FILES`, entry for entry.
- `page/unbound.html` + `page/unbound-worker.js` stand in for a host preparing SoH: Unbound in a
  worker: `soh-extract.js` then `soh-unbound-convert.js` from a ROM, or the converter alone from
  an `oot.o2r`. `unbound-convert.test.ts` holds the base to `oot-unbound.o2r` in
  `SOH_WASM_FILES` byte for byte when that file was converted from the same `oot.o2r`.
- `unit/` tests the pure helpers under `lib/`.
- `knownBug(...)` marks a test for a bug that is not fixed yet. It is skipped unless
  `SOH_WASM_KNOWN_BUGS=1`; the commit that fixes the bug turns it into a plain `test`.
