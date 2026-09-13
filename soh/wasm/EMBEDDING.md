# Embedding the SoH wasm build

The contract between this build and a host page. It is deliberately small: the game reads
its files from an in-memory filesystem and knows nothing about where they came from — no
IndexedDB, no network, no assumptions about the embedder. A host obtains bytes however it
likes and hands them over.

## Building

```
emcmake cmake -H. -Bbuild-wasm-rel -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSOH_WASM_ASSET_DIR=/path/to/o2r/files \
  -DSOH_WASM_HOST_FILES=ON
make -C build-wasm-rel soh -j10
```

Outputs `soh.js`, `soh.wasm` (24.7 MB raw, **4.2 MB brotli**) and `soh.data` (4.55 MB).
Serve all three plus your host page from the same directory.

Without `SOH_WASM_HOST_FILES` the archives are baked into `soh.data` instead — convenient
for local testing, useless for an embedder.

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

`soh/wasm/host.html` is a worked example.

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

The build knows nothing about its embedder. It talks through two plain browser mechanisms,
both defined in `soh/soh/EmbedderBridge.cpp`:

**Outbound: `CustomEvent('soh')` on `window`.** `event.detail` is a plain object with a
`type` and a few fields:

| `detail.type` | Fields | Fired when |
|---|---|---|
| `load-game` | `fileNum` | A save was loaded from file select (or the debug map select). From here on, console commands that need a play state are safe. |
| `scene` | `sceneNum`, `entranceIndex` | A scene finished initialising — after every load, warp, and door. |
| `file-saved` | `path`, `bytes` | The game wrote its config or a save. `path` is absolute in the VFS (`/shipofharkinian.json`, `/Save/file1.sav`, `/Save/global.sav`); `bytes` is a `Uint8Array` copy of the whole file, yours to keep. Persist it however you like — the VFS itself is gone on reload. |
| `quit` | | The game closed its window and the main loop has stopped for good. The last frame stays on the canvas; nothing else happens unless you act. Any file written on the way out arrives as `file-saved` before this. |
| `error` | `message` | A C++ exception escaped a frame. The loop stops after this, so treat it like `quit` with a reason. |

The `file-saved` detection is a watch, not a hook: after each frame the bridge compares the
timestamp and size of the config and of every file under `Save/` against the previous
frame, so every writer in the game is covered without being told about the bridge. The
files the host supplied at boot are the baseline and are not reported.

```js
window.addEventListener('soh', ({ detail }) => {
  if (detail.type === 'scene') showScene(detail.sceneNum, detail.entranceIndex);
});
```

**Inbound: `Soh_RunConsoleCommand`.** Runs one line through the game's debug console, the
same one behind the in-game GUI, and returns that command's result: `0` on success by
convention, `-1` if called before the game has started, `-2` for a command the console
does not know.

```js
Module.ccall('Soh_RunConsoleCommand', 'number', ['string'], ['entrance cd']);
```

`entrance <hex>` is the warp. It needs a play state: after `load-game` it goes where you
say; on the title screen it warps the attract demo instead (that is a play state too); on
file select it refuses with result `1` and a message in the console. Everything else in
`soh/soh/Enhancements/debugconsole.cpp` works the same way. The call lands between frames,
so a handler that queues work for the next frame behaves exactly as when typed.

`host.html` wires both up as an example: it logs every event and defines
`soh('entrance cd')`.

## Paths, and why they look like that

Under Emscripten `Ship::Context::GetAppDirectoryPath()` falls through to `"."`, so the game
looks for its config and saves at the filesystem root — `./shipofharkinian.json`,
`./Save/file1.sav`. That is why the paths above are what they are.

## Settings worth knowing about

The defaults are already right for a browser; there is nothing to add to a config for
speed. What matters is what *not* to turn on:

| CVar | Keep at | Why |
|---|---|---|
| `gSettings.InterpolationFPS` | 20 | The game's logic tick is 20 Hz. Raising this renders extra interpolated sub-frames per tick, and only the last survives the frame callback — pure cost. |
| `gSettings.MatchRefreshRate` | off | On, it pushes interpolation to the display's rate: 3x the rendering per tick on a 60 Hz panel. |
| `gSettings.VsyncEnabled` | either | Now inert here (see below). Historically, turning it off fast-forwarded the game. |

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
  crash has already been found and fixed this way; see `wasm-port.md`, "Other traps". The host pages take `?memtrace`, which
  samples the wasm heap (`Module.HEAPU8`, exported for this), the JS heap, and — when the page
  is cross-origin isolated — the whole renderer, once a second into `localStorage`; after the
  crash, reload with `?memtrace` and the previous run is printed as a table, or read
  `JSON.parse(localStorage.getItem('soh-memtrace-prev'))` by hand. The wasm heap is capped
  at 2 GB (Emscripten's default `MAXIMUM_MEMORY`); hitting it aborts with a message rather
  than killing the tab.

## Limits you should know about

- **Writes do not persist.** The filesystem is in-memory: a save the game writes is gone on
  reload. Handing saves back to the host is not implemented yet; it needs a hook at
  SaveManager's write site.
- **No boot-to-scene.** The game starts at the title screen. Warping to a named scene at
  boot is a separate feature, and needs SoH: Unbound's `SceneDB` to resolve a scene by name.
- **20 fps during gameplay**, but not everywhere: the loop follows `R_UPDATE_RATE`, so the
  pause menu runs at 30 Hz and the title and map-select screens at 60. See `wasm-port.md`
  §1 and §1c.
- **The canvas renders at CSS resolution, not device resolution.**
  `SDL_WINDOW_ALLOW_HIGHDPI` is deliberately not requested. With it, SDL sizes the canvas
  backing store by `devicePixelRatio` while ImGui keeps reporting CSS pixels, and the two
  get mixed — every internal resolution except 100% rendered at double size and cropped on
  a 2x display. Raise the internal resolution multiplier for a sharper picture instead.
- **VSync is inert.** Emscripten's SDL implements the swap interval by retiming the main
  loop, so the setting used to fast-forward the game. Both call sites are now compiled out
  and pacing is owned by `emscripten_set_main_loop`; the toggle does nothing either way.
- **Keyboard only.** Controllers are untested, and `gamecontrollerdb.txt` is not shipped, so
  SDL logs a harmless mapping-load failure at boot.
