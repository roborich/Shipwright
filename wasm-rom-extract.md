# ROM → o2r in the browser — plan for a wasm extractor

Companion to `wasm-port.md`, which explicitly scoped in-browser extraction *out* ("assets
are provided, never extracted"). This document is the survey for bringing it back in as a
**separate artifact**: a small module that turns a ROM into `oot.o2r` / `oot-mq.o2r`, with
an interface that knows nothing about who is calling it. A host runs it in a worker and
keeps the result however it likes.

Status: **built and verified 2026-09-14** (same day as the survey). `soh-extract.js` +
`soh-extract.wasm` build from `soh/wasm/extract/`, the archive they produce is entry-for-entry
identical to the desktop extraction of the same ROM, and the game's wasm build boots on it.
See "What actually happened" at the end; the survey below is left as written.

## What extraction is today

On desktop the game does it in-process, in four layers:

1. `soh/soh/Extractor/Extract.cpp` — finds and validates the ROM (size 32/54/64 MB,
   not-a-zip check, header CRC → version table, CRC32C of the whole file against a list of
   21 known-good dumps, one byte-patch for the MQ debug ROM), then builds a 22-element
   `argv` and calls `zapd_report()`. Everything else in that file is desktop UI: SDL message
   boxes, a native file picker, directory scanning.
2. `ZAPDTR/ZAPD` (`zapd_report`, `Main.cpp`) — mode `ed` ("extract directory"): loads the
   ROM into memory, then parses every XML in `assets/xml/<version>/` (547 files) against it.
   **It is already single-threaded**: `HandleExtract` sets `singleThreaded = true`
   unconditionally and runs the files in a plain loop.
3. `OTRExporter` — registered into ZAPD as the `OTR` exporter set. Each parsed resource is
   serialised into an in-memory `files` map; at `ExporterProgramEnd` the map is written out.
4. `ExporterArchiveO2R` — a libzip archive built from `zip_source_buffer`s, closed once at
   the end. Also writes a `version` (ROM CRC) and `portVersion` (SoH major.minor.patch)
   entry, which the game checks at boot.

Inputs at run time, relative to the working directory (the desktop symlinks
`<install>/assets` into a temp dir and `chdir`s there):

| Path | Size | Notes |
|---|---|---|
| `assets/xml/<version>/` | 3.6–4.0 MB each, 14 versions, **54 MB total** | the extraction recipe |
| `assets/Config_<version>.xml`, `assets/symbols/`, `assets/TexturePool.xml`, `assets/filelists/` | 300 KB | shared |
| the ROM | 32/54/64 MB | read whole into a vector |

Measured on this machine (`ZAPD.out`, NTSC 1.0 ROM, Debug build of ZAPD, release-mode
flags not checked):

| | |
|---|---|
| wall time | 9.1 s (7 s reported by ZAPD for the file loop) |
| peak RSS | 514 MB |
| output | `oot.o2r`, 33.1 MB, 38,390 entries |
| XML files parsed | 547 |

A single-threaded wasm build with a cold V8 tier-up is usually 2–4× a native debug binary
for this kind of code, so plan on **20–40 s in a worker**, to be measured.

## What already ports, and what doesn't

Surveyed against the existing `wasm` branch, which already provisions most of the
dependency graph for the game.

Works as-is:

- **Dependencies.** ZAPD + OTRExporter need tinyxml2, spdlog, libzip, zlib and StormLib
  (compiled in, not exercised: `Main.cpp` always constructs the O2R archive). All five are
  already built for wasm by `CMake/emscripten-deps.cmake` and LUS. The only new one is
  **libpng** (ZAPD's `ImageBackend`, used for PNG modes we never run but linked
  unconditionally); Emscripten ships a `libpng` port.
- **Threading.** Extraction is already single-threaded (above). The `std::mutex` uses are
  harmless without pthreads.
- **File I/O.** All through `std::ifstream`/`ofstream`, `std::filesystem` and libzip's own
  `fopen` — all fine on MEMFS. `chdir` works there too.
- **Exceptions.** ZAPD throws on malformed input; the wasm branch already builds with
  `-fwasm-exceptions`.
- **CRC32C.** `FastCrc32C.c` has a table-driven fallback when no intrinsic is available
  (`NO_CRC_INTRIN`), so the ROM validation compiles for wasm untouched.
- **libgfxd** (bundled C) and **CrashHandler** (`execinfo.h` exists in Emscripten's libc).
- **LUS coupling** is thin: OTRExporter needs `CRC64()` (`StrHash64.cpp`), the
  `ResourceType` enums and `BitConverter` (headers). Linking the already-built
  `libultraship` static archive pulls in only what is referenced; no window, GL or SDL
  objects come along.
- **Progress without a hook.** `ExtractFunc` prints `(i / N): <xml path>` once per file to
  stdout, which reaches `Module.print` synchronously. That is a usable progress signal with
  no source change.

Blocks, each small:

1. **`ctpl::thread_pool` is constructed even though it is never used**
   (`ZAPDTR/ZAPD/Main.cpp:662`). Its constructor spawns `hardware_concurrency()/2` threads;
   without `-pthread`, `pthread_create` fails and `std::thread` throws. One-line fix: build
   the pool only when `!singleThreaded` (which is never, in this mode).
2. **ZAPD's CMakeLists adds `-pthread` on the non-Darwin path** and does
   `find_package(PNG REQUIRED)`. Under Emscripten, `-pthread` would switch the whole module
   to SharedArrayBuffer mode, which the game build deliberately avoids. Either gate both in
   the submodule, or (preferred) don't use ZAPD's CMakeLists at all for the wasm target and
   list its sources from our own — the list is short and already explicit.
3. **`Extract.cpp` mixes the reusable core with SDL UI.** The version table, the CRC list,
   `GetZapdVerStr`, `IsMasterQuest`, the not-compressed check and the argv construction are
   pure; the rest is message boxes and file pickers, and the file includes
   `SDL_messagebox.h` at the top. Split it: `RomInfo.{h,cpp}` (pure, testable, no SDL) used
   by both the desktop `Extractor` and the wasm entry point. The desktop side keeps its
   behaviour byte-for-byte.
4. **Submodule ownership.** `ZAPDTR` and `OTRExporter` are pinned to `harbourmasters`
   upstream with no fork remote; only `libultraship` has the `roborich` fork + `wasm`
   branch pattern. Fix 1 needs a source change, so it needs the same pattern:
   `roborich/ZAPDTR` branch `wasm`, one commit. (The alternative — shadowing `ctpl_stl.h`
   with a stub via include order — works but is the kind of trick the next reader curses.)
5. **The `OTR` exporter set registers itself from a static initialiser**, which is why every
   platform links OTRExporter with `--whole-archive`. `wasm-ld` supports the flag; just
   remember it, or the exporter silently isn't there and ZAPD extracts nothing.

Nothing in the decomp, the game, or the GUI is involved. This is a second, independent
Emscripten target that shares the dependency build.

## Proposed shape

### Artifacts

```
soh-extract.js      glue (MODULARIZE, worker-friendly, no window/canvas)
soh-extract.wasm    ZAPD + OTRExporter + libzip + tinyxml2 + libpng + StrHash64
soh-extract.data    assets/xml/* (14 versions) + Config/symbols/filelists/TexturePool
```

The data file is 54 MB raw and about 4.4 MB gzipped (measured with tar+gzip; brotli will be
smaller). Shipping all 14 versions in one file is the simplest thing that keeps the module
host-agnostic — it detects the version itself. If the fetch ever matters, the follow-up is
one `.data` per version selected from the ROM header before load; not needed for a first
pass, and it would leak version knowledge into the loader.

Built from the same `emcmake` configure as the game, behind an option
(`-DSOH_WASM_EXTRACTOR=ON`), from a new `soh/wasm/extract/CMakeLists.txt`. Root CMake stops
gating `ZAPDTR`/`OTRExporter` on `NOT EMSCRIPTEN` and instead gates them on the option (the
sources are listed by our own target, see block 2). Output lands next to `soh.js`, and
`HOST-API.md` is copied there already, so a host copying "the wasm artifacts" gets both.

**Version pinning matters more than for the game.** The extractor writes `portVersion`
into the o2r, and the game refuses an archive from an incompatible port version
(`OTRGlobals::RunExtract` → `VerifyArchiveVersion`, also in the browser). The extractor
must be built from the same SoH tree as the `soh.wasm` it feeds, and a host should copy the
two together. Worth stating in HOST-API.md in so many words.

### Native entry point

One exported C function, file-based so it stays trivially callable from any glue:

```c
// Reads /rom.z64 from the VFS, writes /out/<oot|oot-mq>.o2r. Returns 0 on success, or a
// small negative code; the failure reason is also printed. Progress goes to stdout as
// "(i / N): path" lines (ZAPD's own).
int Extract_RomToO2r(const char* romPath, const char* outDir);
```

Behind it, in order:

1. `RomInfo` validates: size, not-compressed, header CRC known, full CRC32C in the good
   list, MQ-debug byte fix. Same code path as desktop.
2. Stage the working dir: `/work/assets` is the preloaded tree, `chdir("/work")`.
3. Build the same argv `CallZapd` builds today (`ed -i assets/xml/<ver> -b <rom> -fl
   assets/filelists -gsf 0 -rconf assets/Config_<ver>.xml -se OTR --otrfile <name>
   --portVer <maj.min.patch> ...`), call `zapd_report`.
4. Move the archive to `outDir`.

`zapd_report` allocates a fresh `Globals` per call and never frees the old one, and the
exporter keeps process-lifetime statics (`archive`, `files`). Treat the module as
**one-shot**: one conversion per instantiation, then the worker is terminated. A second call
in the same instance is untested upstream and there is no reason to make it work.

### JS wrapper (shipped as `--post-js`, part of the artifact)

```js
// Inside a worker:
const createExtractor = (await import('./soh-extract.js')).default;
const mod = await createExtractor();                 // fetches .wasm and .data
const result = await mod.extractRom(romBytes, {      // Uint8Array; .z64/.n64/.v64 all fine
  onProgress: (done, total) => postMessage({ done, total }),
});
// result = { name: 'oot.o2r' | 'oot-mq.o2r', version: 'NTSC N64 1.0', bytes: Uint8Array }
```

`extractRom` writes the bytes to `/rom.z64`, parses `Module.print` for the `(i / N)` lines,
calls `Extract_RomToO2r`, and reads `/out/<name>` back out of MEMFS. Errors reject with the
message the C side printed (`Invalid Rom Size`, `Rom CRC invalid`, `File is Compressed`...
the desktop strings, reused). Byte-swapped ROMs are handled by `RomToBigEndian` as today,
so the host needn't care about `.v64` vs `.z64`.

No IndexedDB, no `postMessage` protocol, no knowledge of the caller: the host owns the
worker and the storage. That is the whole contract, and it goes into `HOST-API.md` as a
new section when it exists.

### Memory

Desktop peaks at 514 MB RSS for a 32 MB ROM (the ROM vector, per-file raw data copies,
the `files` map holding every serialised resource, then libzip's output). On top of that,
MEMFS holds the ROM bytes and the finished o2r outside the wasm heap. Start with
`-sINITIAL_MEMORY=256MB -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2GB` and measure; a
64 MB debug ROM is the worst case. This is the one number that could exclude low-end
mobile, and it is inherent to how OTRExporter batches everything before writing. Reducing
it (streaming files into the zip as they finish) is a real change to OTRExporter and is
**not** part of the first pass.

## Work plan

**M1 — it links** (most of the risk). Fork `ZAPDTR` → `roborich/ZAPDTR` branch `wasm` with
the thread-pool guard; point the submodule at it. Add `soh/wasm/extract/CMakeLists.txt`
listing ZAPD + OTRExporter sources, `--use-port=libpng`, `--whole-archive` for the
exporter objects, no `-pthread`. Extract `RomInfo` out of `Extract.cpp` (desktop still
builds and behaves the same). Done when `soh-extract.wasm` links.

**M2 — it converts.** `Extract_RomToO2r` + the post-js wrapper. Done when a ROM fed from a
headless test comes back as an o2r that is byte-identical, entry for entry, to the desktop
build's `build-cmake/soh/oot.o2r` for the same ROM (compare zip listings + per-entry CRCs;
the archive itself will differ in timestamps). Also done: the produced o2r boots the
existing `soh.wasm` in the smoke test. Record wall time and peak heap here.

**M3 — it ships.** `.data` packaging of all 14 versions, output next to `soh.js`,
HOST-API.md section (API, one-shot rule, version-pinning rule, sizes, timing), a
`soh/wasm/tests` case that runs the worker path end to end, and the memory note in the
docs index. Prelude's copy step then picks up the three new files with the existing ones;
that side is Prelude's agent's job.

Rough size: M1 is a day of build wrangling if the LUS experience is representative; M2 and
M3 are each smaller than that. The measured desktop run is the yardstick for "is the
browser version acceptably fast", and 20–40 s inside a worker with a progress bar is.

## Open questions, deliberately left

- **Debug ROMs (54/64 MB) in the browser** — the memory number above is for a 32 MB ROM.
  Measure before promising these.
- **Release-mode ZAPD timing** — the 9 s is a Debug (`-O0`) ZAPD; the wasm build would be
  Release. The browser estimate may be pessimistic.
- **Should the game build learn to call the extractor?** No. Keeping them separate keeps
  `soh.wasm` free of ZAPD (it is ~half the size of the game module in source) and lets a
  host convert once and store, which is what every host wants anyway.

## What actually happened

Milestones 1–3 landed in one session. Where the build differed from the plan:

- **Three fork commits, not one.** `roborich/ZAPDTR` branch `wasm`: the thread-pool guard as
  planned, plus **determinism fixes** the byte comparison forced out. ZAPD writes three
  fields it never initialises for some inputs (`Struct_800A5E28::totalVtxCount`/`dlist` on
  non-skin limbs, `SetMesh::data`, `RoomShapeImageMultiBgEntry::unk_00`/`id` in the single
  layout). A fresh native heap reads them as zero by luck; a reused wasm heap does not, and
  2,981 of 38,390 entries differed on the first run. Zeroed in the fork, the two archives
  match entry for entry (names, sizes, CRCs). `OTRExporter` needed no fork.
- **The crash handler is out**, not stubbed in the fork: Emscripten has no `execinfo.h`.
  `CrashHandler.cpp` is excluded and `RomExtractor.cpp` carries the no-op `CrashHandler_Init`.
- **libpng port bug**: with `-fwasm-exceptions`, emsdk 6.0.9's libpng port script prints a
  Python bool into its own flags (`-sWASM_LEGACY_EXCEPTIONS=True`) and emcc rejects it. Passing
  `-sWASM_LEGACY_EXCEPTIONS=1` explicitly on the compile and link lines is the whole fix.
- **Embedded, not preloaded.** The `.data` approach hit a real host problem: the file packager
  fixes the data URL at the top of the module factory, before any pre-js, and in a worker it
  resolves against the *worker script's* URL. A host whose worker is not in the module's
  directory (any bundled app) got a 404 and a promise that never settled. `--embed-file` puts
  the recipe in the wasm's data segments instead: two artifacts, self-locating, 37 MB raw but
  0.8 MB brotli.
- **Two exported functions**, `Extract_RomToO2r(romPath, outDir)` and `Extract_ResultJson()`,
  wrapped by `extractRom(bytes, {onProgress, quiet})` in `api.js`. Progress is parsed from
  ZAPD's stdout lines as planned; no hook was needed.

Measured (NTSC 1.0, 32 MB ROM, Release build):

| | |
|---|---|
| Chromium module worker | 9.4–9.6 s |
| node 22 / bun 1.4 in-process | 11 s |
| desktop ZAPD (Debug) | 9.1 s |
| node RSS at the end | ~780 MB |
| output | 33,140,924 bytes, 38,390 entries, identical to desktop |

The browser estimate was pessimistic: a Release wasm build matches a Debug native one.
Memory is as predicted and remains the one thing to watch on small devices.

Tests: `soh/wasm/tests/extract.test.ts` (needs `SOH_WASM_ROM`). Note that bun 1.2 crashed
silently inside the synchronous conversion; the suite's existing bun ≥ 1.4 requirement covers it.
