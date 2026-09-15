# Embedding the SoH wasm build

The contract between this build and a host page. It is deliberately small: the game reads
its files from an in-memory filesystem and knows nothing about where they came from — no
IndexedDB, no network, no assumptions about the embedder. A host obtains bytes however it
likes and hands them over.

## Building

```
emcmake cmake -H. -Bbuild-wasm-rel -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSOH_WASM_ASSET_DIR=/path/to/dir/with/soh.o2r
make -C build-wasm-rel soh -j10
```

Outputs `soh.js`, `soh.wasm` (24.7 MB raw, **4.2 MB brotli**) and `soh.data` (4.55 MB).
Serve all three plus your host page from the same directory.

The same configure also builds the ROM -> o2r converter, `soh-extract.js` + `soh-extract.wasm`
(37 MB raw, 4.1 MB gzip, **0.8 MB brotli**: it is mostly the 14 near-identical XML recipe
sets, which compress into almost nothing), next to `soh.js`. `-DSOH_WASM_EXTRACTOR=OFF` skips
it. It is a separate module a host runs in a worker; the contract is HOST-API.md §6 and the
design notes are `wasm-rom-extract.md` in the repo root. The two must come from the same
build: the archive it writes carries the port version, and the game refuses one from any
other.

`soh.data` holds `soh.o2r` and nothing else. The game archive, config and saves always come
from the page, so the output contains nothing extracted from a ROM and can be published as is.
For local play, `bun run serve` in `soh/wasm/tests` serves the build with `host.html`.

## The contract

Set `Module.shipFiles` **before** `soh.js` loads. Keys are absolute VFS paths, values are
`Uint8Array`:

```js
Module.shipFiles = {
  '/oot.o2r':              bytes,  // required: the game archive
  '/shipofharkinian.json': bytes,  // optional: config (CVars)
  '/Save/file1.sav':       bytes,  // optional: a save file
  '/Save/global.sav':      bytes,  // optional
  '/mods/my-scene.o2r':    bytes,  // optional: a mod layer, see below
};
```

Parent directories are created as needed. The files are written during `preRun`, before
`main()`, which is early enough for the archive manager to find them.

`soh/wasm/host.html` is a worked example. Hand over only the files you have: bytes that are
not a save (an HTTP error page, say) are moved aside as `file<N>-<timestamp>.bak` with a
popup, not loaded. A missing or incompatible `oot.o2r` stops the game at startup with an
`error` event instead of a black canvas.

### What the host does *not* supply

`soh.o2r` ships inside the module. It is built from this repo and version-checked against
`gBuildVersion`; a host supplying it could only pair a stale port archive with a newer
binary. Leave it alone.

## Mods

Write a mod archive to `/mods/<name>.o2r` and it is loaded at boot, after the game archive,
so its files win. No extra call and no SoH change: `InitMods()` (during `InitOTR`) scans
that directory, auto-enables anything it has not seen before, and adds each archive.
Verified with a real Prelude export passed through `shipFiles`.

Prefer this over merging mod content into `oot.o2r`. Both work — SoH resolves files through
one flat namespace with last-archive-wins, so a merged file and a stacked layer are
indistinguishable to the game — but merging means rebuilding and handing over a ~33 MB
archive on every iteration, where a layer is only the delta.

Load order within `/mods` is the archives' sorted filename order.

## Talking to the game

The events the game sends (`load-game`, `scene`, `file-saved`, `quit`, `error`) and the
commands a host can send in (`Soh_RunConsoleCommand`) are listed in **`HOST-API.md`**. The
build copies that file next to `soh.js`, so embedders get it with the artifacts. The code is
`soh/soh/EmbedderBridge.cpp`.

`file-saved` and `file-removed` are a watch, not a hook. After each frame the bridge compares
the timestamp and size of the config and of every file under `Save/` with the previous frame. That covers every
writer in the game without any of them knowing about the bridge.

`host.html` wires both directions up as an example: it logs every event and defines
`soh('entrance cd')`.

## Paths, and why they look like that

Under Emscripten `Ship::Context::GetAppDirectoryPath()` falls through to `"."`, so the game
looks for its config and saves at the filesystem root — `./shipofharkinian.json`,
`./Save/file1.sav`. That is why the paths above are what they are.

## Settings worth knowing about

The defaults are already right for a browser; there is nothing to add to a config for
speed. Settings that would cost more than they give are ignored here, so a desktop config
cannot turn them on:

| Setting | Here | Why |
|---|---|---|
| `gSettings.InterpolationFPS`, `gSettings.MatchRefreshRate` | ignored, hidden | One frame is drawn per game tick. Interpolated sub-frames would each be drawn and only the last would reach the screen: up to 3x the rendering per tick for nothing. |
| `gSettings.VsyncEnabled` | ignored, hidden | Emscripten's SDL implements it by retiming the main loop, which fast-forwarded the game. |
| `Window.AudioBackend` | `webaudio` unless the config says `sdl` | A desktop config names `coreaudio` or `wasapi`, which this build does not have. It used to mean a silent game; LUS now uses the platform's own backend and writes that back. Here that is the Web Audio player, whose AudioWorklet runs on the browser's audio thread. SDL's player is kept as the fallback, but its callback runs on the main thread, so with it any frame longer than about 20 ms crackles (see `wasm-port.md`, "Other traps"). |

MSAA and the internal resolution multiplier are the levers with real cost if tuning is ever
needed; both scale fragment work directly. The multiplier is also how you get a sharper
picture on a high-DPI display: the canvas deliberately renders at CSS resolution here (see
below), so supersampling is opt-in rather than automatic.

Note that a config copied from a desktop install carries everything with it, cheats
included — `gCheats.FreezeTime` in particular will freeze the in-game clock here too.
`gSettings.EnabledMods` is the one thing that is healed rather than honoured: names with no
archive under `/mods` are dropped at boot (and the trimmed list is written back), so a
desktop mod list cannot break the Mod Menu here.

## Diagnosing a crash or a freeze

- **The game freezes, the tab lives.** A C++ exception escaped the frame; Emscripten stops
  the main loop. The console has a `[error] Unhandled exception in frame: <what()>` line
  naming it, and Chrome's "Pause on exceptions" (all, not just uncaught) stops at the throw
  with a readable wasm stack — function names are kept in the binary (`--profiling-funcs`).
- **The tab dies ("Aw, Snap").** The renderer process was killed, which no in-page handler
  survives. `chrome://crashes` has the error code, and on macOS the minidumps in
  `~/Library/Application Support/Google/Chrome/Crashpad/completed/` hold more: the
  crashing thread's name, the exception, and — when V8 was compiling wasm — a
  `wasm-function#N` string naming the function (map N through the name section). One such
  crash has already been found and fixed this way; see `wasm-port.md`, "Other traps". `host.html` takes `?memtrace`, which
  samples the wasm heap (`Module.HEAPU8`, exported for this), the JS heap, and — when the page
  is cross-origin isolated — the whole renderer, once a second into `localStorage`; after the
  crash, reload with `?memtrace` and the previous run is printed as a table, or read
  `JSON.parse(localStorage.getItem('soh-memtrace-prev'))` by hand. The wasm heap is capped
  at 2 GB (Emscripten's default `MAXIMUM_MEMORY`); hitting it aborts with a message rather
  than killing the tab.

## Limits you should know about

- **Writes do not persist on their own.** The filesystem is in-memory: a save the game
  writes is gone on reload unless the host keeps the `file-saved` copy (see `HOST-API.md`).
- **Scenes are addressed by entrance index, not by name.** `HOST-API.md` has two ways to
  start in a scene (the `warp` command, or a boot warp point in the config), both by entrance
  index. Resolving a scene by name needs SoH: Unbound's `SceneDB`, which this branch lacks.
- **20 fps during gameplay**, but not everywhere: the loop follows `R_UPDATE_RATE`, so the
  pause menu runs at 30 Hz and the title and map-select screens at 60. See `wasm-port.md`
  §1 and §1c.
- **The canvas renders at CSS resolution, not device resolution.**
  `SDL_WINDOW_ALLOW_HIGHDPI` is deliberately not requested. With it, SDL sizes the canvas
  backing store by `devicePixelRatio` while ImGui keeps reporting CSS pixels, and the two
  get mixed — every internal resolution except 100% rendered at double size and cropped on
  a 2x display. Raise the internal resolution multiplier for a sharper picture instead.
- **No frame interpolation, no VSync.** The loop is paced by the page's timer at the game's
  own rate, one frame per tick, so interpolated frames would only be drawn and thrown away.
  The Current FPS, Match Refresh Rate and Enable Vsync settings are hidden, and a config that
  sets them is ignored. (Emscripten's SDL also implements the swap interval by retiming the
  main loop, which used to fast-forward the game; those call sites are compiled out.)
- **Keyboard and controllers.** A controller works through SDL's Gamepad API backend (played
  on an iPhone), but `gamecontrollerdb.txt` is not shipped, so SDL logs a harmless
  mapping-load failure at boot and unusual controllers may map oddly.
- **Audio starts on the first key, pointer, or touch.** Browsers keep an AudioContext
  suspended until the page has had a user gesture, and a gamepad button is not one, so a
  controller-only session needs one touch of the screen first. After that the player resumes
  itself whenever the context stops running: the tab coming back, or on iOS a phone call,
  Siri, or a headphone change. Until then the game runs silent and its audio queue caps at
  the drop threshold.
