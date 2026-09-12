# SoH in the browser — WebAssembly port plan

Working plan for branch `wasm` (SoH `cb71e22a7` = tag `9.2.3`; LUS `wasm` off `fdcaf633`).
Goal: SoH running in a browser tab so Prelude of Light can boot a user's scene edits
in-game. Supersedes the threading analysis in `prelude-integration.md` (branch
`prelude-integration`), which assumed a far more multithreaded program than this one is.

## Scope for the first pass

Deliberately narrow, so the port is a port and not a rewrite:

- **Assets are provided, never extracted.** The user supplies an already-extracted
  `oot.o2r`. In-browser ROM extraction is a separate project and may never be needed —
  Prelude's users have an o2r by definition. This mirrors what the console targets
  already do (see "Amputations").
- **Keyboard first, controllers later.** LUS already treats keyboard as a first-class
  input device (`libultraship/src/ship/controller/controldevice/controller/mapping/keyboard/`),
  so this costs nothing beyond not chasing gamepad bugs early.
- **Enhancements and GUI stay compiled in.** See "What not to strip" — removing them is
  more work than keeping them.
- **Single-threaded.** No pthreads, no SharedArrayBuffer, no COOP/COEP. See below.

## Why this is smaller than it looks

Three findings from surveying the tree, in descending order of importance.

### 1. The frame loop is already re-entrant

`soh/src/code/graph.c:437` — `RunFrame()` was restructured into a resumable state machine
for the PC port, tagged `// SOH [Port] Game State management for our render loop`:

```c
static struct RunFrameContext {
    GraphicsContext gfxCtx;
    GameStateOverlay* nextOvl;
    GameStateOverlay* ovl;
    int state;
} runFrameContext;
```

with `switch (runFrameContext.state) { case 1: goto nextFrame; }` at the top. That is
exactly the shape `emscripten_set_main_loop` needs. `Graph_ThreadEntry` (graph.c:518) is
just `while (WindowIsRunning()) RunFrame();`.

Consequence: **no Asyncify** (which costs binary size and speed) and **no
`-sPROXY_TO_PTHREAD`** (which would drag in SharedArrayBuffer and COOP/COEP headers).

### 2. The N64 threads are inert

`osCreateThread` / `osStartThread` (`soh/src/libultra/os/createthread.c`,
`startthread.c`) are the untouched decomp originals: they populate `OSThread`
bookkeeping and never context-switch on PC. `soh/src/code/main.c:138` creates and starts
the graph "thread" and then calls `Graph_ThreadEntry(0)` directly on the main thread.
LUS's `os_mesg.cpp` message queues are non-blocking stubs — `osRecvMesg` returns
immediately when empty.

### 3. Exactly two real threads

- The audio thread: `soh/soh/OTRGlobals.cpp:1055` (`audio.thread = std::thread(OTRAudio_Thread)`).
- LUS's `BS::thread_pool` in `libultraship/src/ship/resource/ResourceManager.cpp:55`,
  sized from `std::thread::hardware_concurrency()`.

Both get a single-threaded shim rather than a worker.

## The work

### Milestone 1 — compiles and links

- Add an `Emscripten` arm to the platform switch in `CMakeLists.txt` (currently
  `Windows` / `NintendoSwitch` / `Linux` / `Darwin` / `CafeOS` at lines 83–294).
- Exclude host tools from the cross-compile: ZAPD and the OTR-generation targets
  (`CMakeLists.txt:208,234`) build for the host, not the target.
- Amputations, all via the existing `list(FILTER soh__ EXCLUDE REGEX ...)` idiom already
  used in `soh/CMakeLists.txt:146-162` for crowd-control and the speech synthesizers:
  - `soh/Enhancements/crowd-control/` and `soh/soh/Network/` (28 files) — raw sockets do
    not exist in a browser.
  - `soh/Extractor/` — note `soh/CMakeLists.txt:168` already compiles this **headers-only**
    on Switch and Wii U. Emscripten is the same case: assets arrive pre-extracted.
  - The speech synthesizers (no `espeak-ng`, no SAPI, no Darwin `.mm`).
- Point deps at Emscripten ports or source builds; `INITIAL_MEMORY` / `ALLOW_MEMORY_GROWTH`
  sized for game + decoded o2r cache.

Expect this milestone to be a long tail of link errors and to consume most of the
calendar time.

### Milestone 2 — single-threaded shims

- `emscripten_set_main_loop(RunFrame, 0, 1)` in place of the `while (WindowIsRunning())`
  loop, guarded by `#ifdef __EMSCRIPTEN__`.
- Pump audio from the frame callback instead of `OTRAudio_Thread`.
- A synchronous executor in place of `mThreadPool` (ResourceManager.cpp:55, and the
  `submit_task` call sites at 203, 320, 341, 365).
- Skip `GfxWindowBackendSDL2::SyncFramerateWithTime()`
  (`libultraship/src/fast/backends/gfx_sdl2.cpp:644`) — it `nanosleep`s to hit a frame
  deadline, and `requestAnimationFrame` already paces the loop.
- **Boot landmine:** `osContInit` (`libultraship/src/libultraship/libultra/os.cpp:17`)
  loads `gamecontrollerdb.txt` off disk and calls `exit(EXIT_FAILURE)` if
  `SDL_Init(SDL_INIT_GAMECONTROLLER)` fails. Handle before anything else can boot.

### Milestone 3 — boots to title

Pre-mount an `oot.o2r` into the Emscripten filesystem; render through the existing
`USE_OPENGLES` path (`libultraship/src/CMakeLists.txt`, guards throughout
`libultraship/src/fast/backends/gfx_opengl.cpp`), which Emscripten maps to WebGL2.

### Milestone 4 — playable

o2r plus mod layer read from IndexedDB at boot; keyboard input; save persistence via
IDBFS.

### Milestone 5 — boot-to-scene

Authored separately on branch `unbound` (it resolves a scene *name* through
`SceneDB::RetrieveId` / `RetrieveEntranceIndex` in `soh/soh/unbound/SceneDB.h`, which
vanilla's static `gEntranceTable` cannot do), then merged here.

## What not to strip

Stripping the GUI and enhancements is tempting and is the wrong call:

- **369 files** across `soh/soh` and `soh/src` call `CVarGet*`. Enhancements are wired in
  through GameInteractor hooks and `RegisterShipInitFunc`, not isolated behind a flag.
  Removing them produces link errors and dead branches across the tree — strictly more
  work than leaving them compiled.
- ImGui (60 files) renders fine on WebGL2 and is the **only debugging surface** available
  in a browser tab, where no native debugger can attach. The console commands
  (`soh/soh/Enhancements/debugconsole.cpp`) are worth more here than on desktop, not less.

If something in that layer blocks boot, disable it **at runtime via CVar defaults** —
reversible, no deletion. The amputations in Milestone 1 are a different axis: they target
OS services that do not exist in a browser, not UI surface area.

## Open questions

- Memory ceiling. wasm32 caps at 4 GB; game plus decoded o2r cache wants well north of
  512 MB. Unbound raises this further (`GBI_S32_VTX`, `GBI_FLOAT_MTX`, uncapped
  collision, growable text) — measure unbound's desktop RSS before committing to the
  combined target.
- Whether the synchronous ResourceManager executor is fast enough at scene load, or
  whether it visibly hitches.
- Performance. The prior estimate was 20-30 FPS initially; untested.
