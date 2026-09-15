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
Events are dispatched between frames, after the frame that raised them, so a listener can
call `Soh_RunConsoleCommand` directly (a warp from inside a `scene` listener works).

```js
window.addEventListener('soh', ({ detail }) => { switch (detail.type) { /* ... */ } });
```

| `detail.type` | Fields | Meaning |
|---|---|---|
| `load-game` | `fileNum` | A save was loaded and its first scene is up; sent just before that `scene` event. Play-state commands now act on the player's game. `fileNum` is `255` for a game on the fresh debug save, which is what a `warp` outside gameplay, a boot warp, or the Debug Warp Screen starts. |
| `scene` | `sceneNum`, `entranceIndex`, `setup` | A scene finished initialising (boot, load, warp, door). All plain numbers. `entranceIndex` is decimal here, but `entrance` and `warp` take hex: `entranceIndex.toString(16)`. `setup` is the scene layer that loaded: `0` child day, `1` child night, `2` adult day, `3` adult night, `4` and up a cutscene layer. |
| `file-saved` | `path`, `bytes` | The game wrote `/shipofharkinian.json` or a file under `/Save/`. `bytes` is a `Uint8Array` copy of the whole file. Persist it yourself; the VFS is lost on reload. Files you supplied at boot are not echoed back. |
| `file-removed` | `path` | A watched file is gone: a save erased in file select, or one the game moved aside as unreadable (its `file<N>-<timestamp>.bak` arrives as `file-saved`). Delete your copy, or the next boot brings it back. |
| `quit` | — | The main loop stopped for good (for example after the `quit` command). Any final `file-saved` events arrive first. |
| `error` | `message` | The game stopped: a C++ exception escaped a frame, or at startup the game archives were missing or from an incompatible version (then no `scene` ever arrives). Any final `file-saved` events arrive first. Treat it as `quit` with a reason. |

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
| `warp <entrance hex> [adult\|child] [time] [room x y z yaw]` | no | Warp to an entrance as either age at a time of day, optionally standing at a point (see below); a `scene` event follows. Outside gameplay (title screen, its attract demo, file select) it first starts a fresh game on the debug save, so it is how a host boots straight into a scene: send it on the first `soh` event. |
| `reload` | yes | Re-enters the current entrance (reloads the scene). |
| `void` | yes | Void out to the last respawn point. |
| `reset` | no | Back to the title screen. |
| `file_select` | no | Back to file select. |
| `quit` | no | Closes the game: final `file-saved` events, then `quit`. |
| `save_state` / `load_state` / `set_slot <n>` | yes | Emulator-style save states (in memory only). |

### `warp` in detail

`warp <entrance hex> [adult|child] [time] [room x y z yaw]`

The game picks the scene layer from Link's age and the time of day (`z_play.c` `Play_Init`),
so those two arguments choose the setup. Both are optional and default to adult at noon,
which is what the boot warp always loaded before they existed.

- **Age**: `adult` or `child`.
- **Time**: `day` (noon, `8000`) or `night` (midnight, `0`), or the game's own `dayTime` as
  a hex number `0000`–`FFFF` when the lighting should match a particular clock. Night is
  above `C000` or below `4555`; anything between is day.
- **Point**: five numbers, the room number, `x y z` as floats, and the yaw as a decimal
  binary angle (`0x8000` = 180°, so `-1681` or `32767` are both fine). Given, Link stands
  there; omitted, he spawns where the entrance puts him. Pick a point over a floor: the game
  respawns Link at the same point after a void-out, so a floorless one falls forever.

The Freeze Time cheat (`gCheats.FreezeTime` in the config) keeps the clock still; a `warp`
moves it to the time asked for and it stays still there.

Two scenes ignore the time: Hyrule Field as child shows layer 1 only once the three
spiritual stones are held (the debug save does not hold them, so it shows layer 0), and
Kokiri Forest as adult shows layer 2 or 3 by Forest Temple completion. `setup` on the
`scene` event says which layer really loaded.

```js
run('warp 0 child night');                 // Deku Tree, layer 1, at the entrance
run('warp bb adult day 0 0 0 0 0');        // Link's House, layer 2, standing at the origin
```

The console has ~50 more commands (items, health, rupees, `spawn`, `pos`, cheats, …).
Each one is registered with its arguments in `DebugConsole_Init()` in
`soh/soh/Enhancements/debugconsole.cpp`, and they all work here the same way.

## 4. Booting into a scene from the config

`warp` covers a host that can wait for the first `soh` event. A host that would rather not
can set the game up to skip the title screen through the config it hands in, which is what
the game's own Developer Tools > Warp Points feature reads:

```json
{
  "CVars": { "gSettings": { "BootSequence": 4 } },
  "WarpPoints": {
    "my point": {
      "bootToPoint": true,
      "entranceId": 0, "roomNum": 0, "pos": { "x": 0, "y": 0, "z": 0 }, "rotY": 0,
      "linkAge": 1, "dayTime": 0
    }
  }
}
```

`BootSequence` `4` is "Warp Point" (`gDeveloperTools.DebugEnabled` is not needed for it); at boot the game starts a fresh game on the debug save
at the one point marked `bootToPoint`, exactly as `warp <entranceId hex> ... <roomNum x y z
rotY>` would, then sends `load-game` (`fileNum` 255) and `scene`. With no marked point it
opens the Debug Warp Screen instead. `linkAge` is `0` adult or `1` child, and `dayTime` the
`warp` command's time as a number; both may be omitted and then mean adult at noon. The game
writes the block back into the config it saves, so a host that owns the config should set
the point it wants on every boot.

## 5. Diagnostics: `Soh_GetStats` (unstable)

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

## 6. Converting a ROM: `soh-extract.js`

A second, independent module built next to `soh.js`: `soh-extract.js` + `soh-extract.wasm`
(37 MB raw, about 0.8 MB brotli). It turns a ROM into the `oot.o2r` / `oot-mq.o2r` that §1
asks for, as the desktop game would (the same ROM checks, the same ZAPD run, the same
archive entry for entry; it accepts every ROM version ZAPD has a recipe for). It has no window, no canvas and no idea who is calling: run it in a worker, keep
the bytes wherever you like.

```js
// In a module worker (or a page, or node):
const { default: createSohExtractor } = await import('./soh-extract.js');
const mod = await createSohExtractor();
const { name, version, bytes } = await mod.extractRom(romBytes, {
  onProgress: (done, total) => postMessage({ done, total }),
});
// name: 'oot.o2r' | 'oot-mq.o2r'   version: e.g. 'NTSC N64 1.0'   bytes: Uint8Array
```

- `romBytes` is a `Uint8Array` of the whole file in any byte order (`.z64`, `.n64`, `.v64`).
- `onProgress(done, total, info)` runs in two phases, each counting its own units, so a
  host can weight a progress bar by measured time rather than by count:
  - `info.phase === 'recipe'`: `done`/`total` over ZAPD's recipe files (547 for NTSC 1.0),
    one call per file as it starts, `done` counting 1, 2, ... `total`. `info.file` is that
    file's path in the recipe (`assets/xml/N64_NTSC_10/objects/object_link_boy.xml`), stable
    for a ROM version across runs and machines. So `(total, total)` means the last file is
    being extracted, not that the phase is over. This phase is about three quarters of the run.
  - `info.phase === 'write'`: `done`/`total` over the archive's entries (38,390 for NTSC 1.0)
    while libzip writes the archive. It starts with `(0, total)`, ends with `(total, total)`,
    and reports in half-percent steps between. This is the last quarter of the run.
  Expect about 10 s in all, in a worker on a desktop machine. The third argument was added
  after the first release; a host that reads only `done`/`total` still works, but its bar
  fills during the recipe phase and then restarts from zero for the write.
- **One conversion per instance.** Call the factory again for the next ROM; a second
  `extractRom` on the same instance rejects.
- **Rejections** are `Error`s with a numeric `error.code`, worded as the desktop game words
  the same refusals:

  | `code` | Meaning |
  |---|---|
  | `-1` | the file could not be read |
  | `-2` | not 32, 54 or 64 MB |
  | `-3` | looks like a zip / rar / 7z |
  | `-4` | header CRC is not a version this build has a recipe for |
  | `-5` | whole-file CRC is not a known-good dump |
  | `-6` | ZAPD failed part-way; what it said is in the message |
  | `-7` | ZAPD finished but wrote no archive |
  | `-8` | this instance has already converted; make a new one |

  Anything ZAPD printed to stderr during a failed run is appended to the message.
- `quiet: true` keeps ZAPD's stdout out of the console; its warnings on stderr always show.
- The module resolves its own `.wasm` and carries its data inside it, so it can be imported
  from anywhere; `locateFile` still works if you move the `.wasm` elsewhere.
- Memory: about 500–800 MB of heap for a 32 MB ROM (the exporter builds the whole archive in
  memory before writing it). The module starts at 256 MB and grows.

**Build the two together.** The archive records the port version, and the game refuses one
from any other build (§1's `error` event, "extract it again"). Copy `soh-extract.*` from the
same build as `soh.*`.
