# SoH wasm — host API

What a page embedding `soh.js` / `soh.wasm` / `soh.data` can hand in, hear back, and send.
The build knows nothing about its host; everything below is plain browser machinery. The
build copies this file next to `soh.js`, so it ships with the artifacts. Background and
build notes are in `EMBEDDING.md` in the Shipwright repo.

## 1. Files in: `Module.shipFiles`

Set before `soh.js` loads. Keys are absolute VFS paths, values `Uint8Array`:

```js
Module.shipFiles = {
  '/oot.o2r':              bytes,  // required
  '/shipofharkinian.json': bytes,  // optional: config
  '/Save/file1.sav':       bytes,  // optional: saves (file1..3, global.sav)
  '/mods/<name>.o2r':      bytes,  // optional: mod layers, loaded in sorted filename order
};
```

Do not supply `soh.o2r`; it is inside the module.

## 2. Events out: `CustomEvent('soh')` on `window`

Attach the listener **before** `soh.js` loads; the title screen fires events at boot.

```js
window.addEventListener('soh', ({ detail }) => { switch (detail.type) { /* ... */ } });
```

| `detail.type` | Fields | Meaning |
|---|---|---|
| `load-game` | `fileNum` | A save was loaded. Play-state commands now act on the player's game. |
| `scene` | `sceneNum`, `entranceIndex` | A scene finished initialising (boot, load, warp, door). Both are plain numbers. `entranceIndex` is decimal here, but `entrance` takes hex: `entranceIndex.toString(16)`. |
| `file-saved` | `path`, `bytes` | The game wrote `/shipofharkinian.json` or a file under `/Save/`. `bytes` is a `Uint8Array` copy of the whole file. Persist it yourself; the VFS is lost on reload. Files you supplied at boot are not echoed back. |
| `quit` | — | The main loop stopped for good (for example after the `quit` command). Any final `file-saved` events arrive first. |
| `error` | `message` | The game stopped: a C++ exception escaped a frame, or at startup the game archives were missing or from an incompatible version (then no `scene` ever arrives). Treat it as `quit` with a reason. |

## 3. Commands in: `Soh_RunConsoleCommand`

Runs one line through the in-game debug console:

```js
const run = cmd => Module.ccall('Soh_RunConsoleCommand', 'number', ['string'], [cmd]);
run('entrance cd');
```

Return value: `0` success · `1` the command refused (bad arguments, or no play state) ·
`-1` the console isn't up yet · `-2` unknown command. Call it only after the runtime is up;
the first `soh` event is a safe signal. The command takes effect on the next frame.

"Play state" below means gameplay or the title-screen attract demo, but not file select.

| Command | Needs play state | Does |
|---|---|---|
| `entrance <hex>` | yes | Instant warp to an entrance index; a `scene` event follows. |
| `reload` | yes | Re-enters the current entrance (reloads the scene). |
| `void` | yes | Void out to the last respawn point. |
| `reset` | no | Back to the title screen. |
| `file_select` | no | Back to file select. |
| `quit` | no | Closes the game: final `file-saved` events, then `quit`. |
| `save_state` / `load_state` / `set_slot <n>` | yes | Emulator-style save states (in memory only). |

The console has ~50 more commands (items, health, rupees, `spawn`, `pos`, cheats, …).
Each one is registered with its arguments in `DebugConsole_Init()` in
`soh/soh/Enhancements/debugconsole.cpp`, and they all work here the same way.

## 4. Diagnostics: `Soh_GetStats` (unstable)

For tests and profiling, not for building on: the fields can change without notice.

```js
JSON.parse(Module.ccall('Soh_GetStats', 'string'));
// { ticks, draws, updateRate, audioBuffered, audioDrops, sceneNum }
```

`ticks` counts frame-loop callbacks and `draws` the frames drawn in them; more than one draw
per tick means frame interpolation is on. `updateRate` is the game's vsync divisor (the loop
runs at `60 / updateRate` Hz). `audioBuffered` is the number of samples queued for the audio
device, and `audioDrops` the updates discarded because that queue was already full. `sceneNum`
is `-1` outside a play state.
