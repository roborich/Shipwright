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
};
```

Parent directories are created as needed. The files are written during `preRun`, before
`main()`, which is early enough for the archive manager to find them.

`soh/wasm/host.html` is a worked example.

### What the host does *not* supply

`soh.o2r` ships inside the module. It is built from this repo and version-checked against
`gBuildVersion`; a host supplying it could only pair a stale port archive with a newer
binary. Leave it alone.

## Paths, and why they look like that

Under Emscripten `Ship::Context::GetAppDirectoryPath()` falls through to `"."`, so the game
looks for its config and saves at the filesystem root — `./shipofharkinian.json`,
`./Save/file1.sav`. That is why the paths above are what they are.

## Limits you should know about

- **Writes do not persist.** The filesystem is in-memory: a save the game writes is gone on
  reload. Handing saves back to the host is not implemented yet; it needs a hook at
  SaveManager's write site.
- **Mods are not loaded from the VFS.** Putting a mod `.o2r` in `shipFiles` places the bytes
  but nothing reads them: SoH adds mod archives through the Mod Menu
  (`soh/soh/Enhancements/mod_menu.cpp`), not by scanning at boot. Loading a host-supplied
  mod needs a small change here first.
- **No boot-to-scene.** The game starts at the title screen. Warping to a named scene at
  boot is a separate feature, and needs SoH: Unbound's `SceneDB` to resolve a scene by name.
- **20 fps.** The game's logic tick is 20 Hz and the frame loop yields once per tick. See
  `wasm-port.md` §1.
- **Keyboard only.** Controllers are untested, and `gamecontrollerdb.txt` is not shipped, so
  SDL logs a harmless mapping-load failure at boot.
