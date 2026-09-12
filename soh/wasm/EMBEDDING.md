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

## Paths, and why they look like that

Under Emscripten `Ship::Context::GetAppDirectoryPath()` falls through to `"."`, so the game
looks for its config and saves at the filesystem root — `./shipofharkinian.json`,
`./Save/file1.sav`. That is why the paths above are what they are.

## Limits you should know about

- **Writes do not persist.** The filesystem is in-memory: a save the game writes is gone on
  reload. Handing saves back to the host is not implemented yet; it needs a hook at
  SaveManager's write site.
- Nothing below concerns mods: see "Mods" above — they work as-is.
- **No boot-to-scene.** The game starts at the title screen. Warping to a named scene at
  boot is a separate feature, and needs SoH: Unbound's `SceneDB` to resolve a scene by name.
- **20 fps.** The game's logic tick is 20 Hz and the frame loop yields once per tick. See
  `wasm-port.md` §1.
- **Keyboard only.** Controllers are untested, and `gamecontrollerdb.txt` is not shipped, so
  SDL logs a harmless mapping-load failure at boot.
