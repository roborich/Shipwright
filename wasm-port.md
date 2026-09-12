# SoH in the browser — WebAssembly port plan

Working plan for branch `wasm` (SoH `cb71e22a7` = tag `9.2.3`; LUS `wasm` off `fdcaf633`).
Goal: SoH running in a browser tab so Prelude of Light can boot a user's scene edits
in-game. Supersedes the threading analysis in `prelude-integration.md` (branch
`prelude-integration`), which assumed a far more multithreaded program than this one is.

## Scope for the first pass

Deliberately narrow, so the port is a port and not a rewrite:

- **Assets are provided, never extracted.** The user supplies an already-extracted
  `oot.o2r`; the build ships a host-generated `soh.o2r` alongside it, version-pinned.
  In-browser ROM extraction is a separate project and may never be needed — Prelude's
  users have an o2r by definition. This is the same shape as the console targets, though
  their headers-only Extractor build has bit-rotted (see Milestone 1).
- **Keyboard first, controllers later.** LUS already treats keyboard as a first-class
  input device (`libultraship/src/ship/controller/controldevice/controller/mapping/keyboard/`),
  so this costs nothing beyond not chasing gamepad bugs early.
- **Enhancements and GUI stay compiled in.** See "What not to strip" — removing them is
  more work than keeping them.
- **Single-threaded.** No pthreads, no SharedArrayBuffer, no COOP/COEP. See below.
- **20 fps during gameplay, and that is accepted** (decided 2026-09-11). Gameplay's logic
  tick is 20 Hz and the frame loop yields once per tick. Rendering gameplay faster needs a
  second yield point inside the sub-frame loop (§1); explicitly **out of scope**.
  **Correction (2026-09-12):** 20 Hz is not global. `R_UPDATE_RATE` is the vsync divisor
  and the game runs at `60/R_UPDATE_RATE` Hz — 3 while playing, 2 in the pause menu, 1 on
  the title and map-select screens. The loop now tracks it (§1c); pinning it to 20 Hz made
  audio play slow in every state but gameplay.

## What the survey found

Four findings from surveying the tree, in descending order of importance. The first three
make the port smaller than it looks; the fourth is the one that would have bitten us.

### 1. The frame loop is already re-entrant — at one yield point

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

with `switch (runFrameContext.state) { case 1: goto nextFrame; }` at the top.
`Graph_ThreadEntry` (graph.c:518) is just `while (WindowIsRunning()) RunFrame();`.

Consequence: **no Asyncify** (which costs binary size and speed) and **no
`-sPROXY_TO_PTHREAD`** (which would drag in SharedArrayBuffer and COOP/COEP headers).

**But there is exactly one yield point, and it is coarse.** `graph.c:498-501` yields only
*after* `Graph_ProcessGfxCommands` returns — i.e. after every interpolated sub-frame of a
game tick has already been rendered synchronously by the `for (const auto& m :
mtx_replacements)` loop in `RunCommands` (`soh/soh/OTRGlobals.cpp:1719-1723`). Under
Emscripten `SDL_GL_SwapWindow` is a no-op and the canvas presents only when the callback
returns, so N sub-frames per callback display only the last one. Rendering above the game
tick rate needs a *second* yield point inside that loop, which the state machine does not
have. **Plan on 20 fps through Milestone 4 and treat 60 fps as its own work item.**

### 1c. The frame rate is not constant — audio depends on it

`R_UPDATE_RATE` is the N64's vsync divisor. The game runs at `60/R_UPDATE_RATE` Hz *and*
synthesises `R_UPDATE_RATE` audio buffers per frame (`OTRAudio_FillBuffer`), so the two
cancel and audio always lands at 32 kHz. Game states change it:

| State | `R_UPDATE_RATE` | Rate |
|---|---|---|
| Gameplay (`game.c:437`) | 3 | 20 Hz |
| Pause menu (`z_kaleido_setup.c:60`) | 2 | 30 Hz |
| Title, map select (`z_title.c:159`, `z_select.c:1896`) | 1 | 60 Hz |

A loop pinned to one rate breaks the cancellation and audio comes out at
`rate * R_UPDATE_RATE * ~533` samples/sec — at a fixed 20 Hz that is two thirds speed when
paused and one third in map select. `Graph_EmscriptenFrame` re-derives the callback rate
from `R_UPDATE_RATE` after every frame.

This also explains a number that was mistaken for the game's own timing for a whole
session: boot to the first attract-demo scene measured ~24s because the title screen was
running at a third of its intended rate. Corrected, it is 8.1s.

### 1b. Frame pacing is not free — this is the trap

Game logic runs at **20 Hz**, not 60: `GetInterpolationFPS` defaults `InterpolationFPS` to
20 (`OTRGlobals.cpp:996-998`), which with one sub-frame per tick means one `RunFrame` per
20 Hz tick. The only thing pacing that on desktop is
`GfxWindowBackendSDL2::SyncFramerateWithTime()` (`libultraship/src/fast/backends/gfx_sdl2.cpp:644`),
reached per sub-frame from `Interpreter::EndFrame` → `SwapBuffersBegin`
(`libultraship/src/fast/interpreter.cpp:4478-4483`).

So "delete the `nanosleep`, let `requestAnimationFrame` pace it" **runs the game at 3x
speed** on a 60 Hz display, and at other wrong speeds on 120/144 Hz panels. The fix is
either `emscripten_set_main_loop_timing(EM_TIMING_RAF, 3)` (20 fps, still wrong off 60 Hz)
or a time accumulator that skips ticks. Budget this as real work, not a `#ifdef`.

### 2. The N64 threads are inert

`soh/CMakeLists.txt:191` does `list(FILTER src__ EXCLUDE REGEX "src/libultra/os/")`, so the
decomp's `createthread.c` / `startthread.c` are **never compiled**. What actually links are
empty stubs — `osCreateThread` and `osStartThread` at `soh/soh/stubs.c:96-102`, with
`__osDisableInt` / `__osRestoreInt` at `:127-131` likewise empty. (Proof the decomp files
aren't built: their `osStartThread` calls `__osDispatchThread()` / `__osEnqueueAndYield()`,
which have no definition anywhere in the tree.)

`soh/src/code/main.c:138` creates and starts the graph "thread" — both no-ops — and then
calls `Graph_ThreadEntry(0)` directly on the main thread. LUS's `os_mesg.cpp` message
queues are non-blocking stubs: `osRecvMesg` returns immediately when empty, so the ~30
`OS_MESG_BLOCK` call sites in `soh/src/code` never block.

### 3. Few real threads, but more than two

Four are always created, two more on demand. Under Emscripten without `-pthread`, every
`std::thread` construction aborts, so each needs a shim:

| Thread | Where |
|---|---|
| Audio | `soh/soh/OTRGlobals.cpp:1055` |
| LUS ResourceManager pool | `libultraship/src/ship/resource/ResourceManager.cpp:56` (submits at 203, 320, 341, 365) |
| spdlog async logger | `libultraship/src/ship/Context.cpp:105`, `:153` (`async_overflow_policy::block`) |
| SaveManager pool | `soh/soh/SaveManager.cpp:130` (`detach_task` :1225, `wait()` :1331) |
| Randomizer seed gen *(on demand)* | `soh/soh/Enhancements/randomizer/randomizer.cpp:3512` |
| Custom-audio decoders *(on demand)* | `soh/soh/resource/importer/AudioSampleFactory.cpp:313-321` |

## The work

### Milestone 1 — compiles and links ✅ DONE (2026-09-11, 8354f68a2)

- Add an `Emscripten` arm to the ~12 scattered `if(CMAKE_SYSTEM_NAME …)` blocks in
  `CMakeLists.txt` (lines 83-294) and `soh/CMakeLists.txt`.
- Exclude host tools: ZAPD and the OTR-generation targets (`CMakeLists.txt:208,234`), and
  drop the `ZAPDLib` link (`soh/CMakeLists.txt:623,703`) by mirroring the Switch branch at
  `:668-677`.
- **Dependencies.** LUS `find_package`s libzip, nlohmann_json, tinyxml2, spdlog
  (`libultraship/src/CMakeLists.txt:66-75`); StormLib, prism, libgfxd, thread-pool, ImGui
  and stb arrive via FetchContent. soh additionally needs Ogg, Vorbis, **Opus and OpusFile**
  (`soh/CMakeLists.txt:697-700`) — Opus and OpusFile have no Emscripten port and need a
  source build or a stub.
- **Exceptions.** Emscripten disables exception catching by default and any `throw` aborts.
  Live paths: `Config.cpp:207-210`, `SaveManager.cpp:1310/1338`, `ResourceLoader.cpp:169`
  and `JsonFactory.cpp:16` (unguarded `json::parse`), ~30 `std::stoi` catch blocks in
  `debugconsole.cpp`, `Presets.cpp:222`, spdlog init. Budget `-fwasm-exceptions`.
- Amputations, via the existing `list(FILTER soh__ EXCLUDE REGEX ...)` idiom already used
  in `soh/CMakeLists.txt:146-162`:
  - `soh/Enhancements/crowd-control/` and the speech synthesizers.
  - **Networking is a CVar/define, not a deletion.** `SohGui.cpp:35` and
    `SohMenuNetwork.cpp:3-7` include Network headers unconditionally, and `DeinitOTR`
    (`OTRGlobals.cpp:1541-1553`) references the `Sail` / `Anchor` / `CrowdControl`
    instances outside `ENABLE_REMOTE_CONTROL`. The existing guards in `Network.cpp:8,53,66`
    already do the right thing — keep the 37 files compiled with
    `ENABLE_REMOTE_CONTROL` off.
  - `soh/Extractor/` — `soh/CMakeLists.txt:168` compiles this headers-only on Switch and
    Wii U, but **that template is bit-rotted**: `Extractor extract;` at `OTRGlobals.cpp:409`
    is unguarded, and `Messagebox_ShowErrorBox` (`:1447-1449`) calls
    `Extractor::ShowErrorBox`, defined only in `Extract.cpp:112` and reached from
    `soh/src/code/audio_load.c:1422`. The existing guards are `__SWITCH__`/`__WIIU__`
    macros at `OTRGlobals.cpp:47,418,449,462,560,759,909`, so this is source edits, not
    just a CMake filter.
- `INITIAL_MEMORY` / `ALLOW_MEMORY_GROWTH` sized for game + decoded o2r cache.

**Outcome.** `soh.js` + `soh.wasm` build from the full decomp and port layer with
Emscripten 6.0.9. The predicted long tail of link errors did not materialise: the decomp
translated to wasm without a single source change, and the only source edits needed at all
were the two amputations below. Everything else was build plumbing.

What actually had to change, beyond the plan above:

- `-pthread` was being added unconditionally in the final `else()` of the platform chain
  (`soh/CMakeLists.txt:587`), which under Emscripten requests a **shared-memory** build —
  the SharedArrayBuffer/COOP-COEP dependency this target exists to avoid. Now excluded.
- `-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2`, or the GLES3 entry points do not resolve.
- `FMT_CONSTEVAL=constexpr` for spdlog's bundled fmt (the macOS desktop build needs the
  same workaround).
- The `glewInit()` guard, as predicted.
- libzip linked `PUBLIC` rather than `PRIVATE` in LUS: the public header `O2rArchive.h`
  includes `<zip.h>`, which desktop builds get for free from a system include dir.
- `RunExtract`'s body compiled out (see Milestone 2 — this arrived early, because removing
  every `Extractor::` reference was the only way to link).

Caveat on artifact size: the Debug build's `soh.wasm` is **626 MB**, almost entirely DWARF.
A release build has not been measured yet and needs to be before any judgement about
download size.

The one dependency with no answer: **Opus and OpusFile**, stubbed to silence rather than
built from source. That costs custom streamed Opus audio and nothing else.

### Milestone 2 — single-threaded shims ✅ DONE (2026-09-11, 04d4691be)

- ~~**Bypass `RunExtract`.**~~ **Done in Milestone 1.** Its body (`OTRGlobals.cpp:391-757`)
  is compiled out under `__EMSCRIPTEN__`, because removing every `Extractor::` reference
  was the only way to link. That also disposes of its `while (!extractDone)` frame-pump,
  which would have hung the tab: it draws ImGui popups in a loop that never returns to the
  event loop, so the click to dismiss one could never arrive — and it runs *before*
  `Main()`, so it would have blocked everything else.
- ~~**Rewrite the audio handshake, don't just move the thread.**~~ Done — the update body
  is split out as `OTRAudio_FillBuffer`, shared by the thread (desktop) and the frame
  callback (wasm). `Graph_ProcessGfxCommands`
  sets `audio.processing = true` and notifies (`OTRGlobals.cpp:1730-1734`), then blocks at
  the end on `while (audio.processing) audio.cv_from_thread.wait(Lock)`
  (`OTRGlobals.cpp:1780-1785`). With no audio thread this **deadlocks on frame one**.
  Both sites change, not just `OTRAudio_Thread`.
- ~~`emscripten_set_main_loop`~~ Done. Pacing is **20 callbacks/second via setTimeout**
  (`emscripten_set_main_loop(cb, 20, 1)`), not rAF: the tick rate is the game's, so this is
  correct on every display, where rAF would be 3x fast at 60 Hz. Not vsync-aligned — an
  acceptable trade at 20 fps, revisit if it judders.
- ~~Synchronous executors for all four always-on threads.~~ Done, plus the two on-demand
  ones (randomizer generation, streamed-sample decoding), which abort rather than degrade
  if left alone. Both now block while they work.
- ~~Skip `SyncFramerateWithTime()`.~~ Done, together with the replacement pacing above.
- Preload `gamecontrollerdb.txt`: `osContInit`
  (`libultraship/src/libultraship/libultra/os.cpp:14-23`) loads it off disk. Emscripten's
  SDL2 does have a working Gamepad-API joystick backend, so `SDL_Init` succeeds and a
  missing file only logs an error — an annoyance, not a blocker.

### Milestone 2b — boots to an ImGui frame ✅ DONE (2026-09-11, 6ed5b4414)

Before touching the game: stand up SDL + WebGL2 + ImGui alone via
`Fast3dWindow::RunGuiOnly()` (`libultraship/src/fast/Fast3dWindow.cpp:174`). This isolates
the window/render/GUI stack from both the game loop and the o2r, so failures in Milestone 3
have one cause instead of three.

**Outcome.** Verified in headless Chrome: module instantiates, canvas reports a `webgl2`
context, `soh.o2r` is read out of MEMFS, ImGui draws at a steady frame rate, zero WebGL
errors. Built with `-DSOH_WASM_GUI_ONLY=ON`; `OTRGlobals`' constructor turned out to be the
exact seam (resource manager with soh.o2r only, then window, then GUI — everything after is
game).

**The finding that mattered: `USE_OPENGLES` defaults to OFF**
(`libultraship/src/CMakeLists.txt:10`), so this build had been compiling LUS's *desktop* GL
path the whole time, generating desktop GLSL against a WebGL2 context. Every shader failed
to compile; ImGui reported it first as `#version 120`. The tell was visible back in
Milestone 1 — `glewInit()` being compiled in — and was misread as a stray platform guard
rather than the signal that the whole GLES3 path was off. Fixed by setting `USE_OPENGLES`
for this target plus `IMGUI_IMPL_OPENGL_ES3` for ImGui's backend.

Two harmless leftovers seen in the console, noted so they are not re-investigated: a 404
for a favicon, and one `emscripten_set_main_loop_timing: ... a main loop does not exist`
warning emitted during window setup, before the loop is installed.

### Milestone 3 — boots to title ✅ DONE (2026-09-11, f08107159)

Pre-mount `oot.o2r` **and `soh.o2r`** (the port's own asset archive, still produced by a
host ZAPD and version-pinned to the build — `OTRGlobals.cpp:300` initializes the
ResourceManager with `portArchivePath`, and `:322-336` drops fonts on a version mismatch).
Render through the existing `USE_OPENGLES` path, which Emscripten maps to WebGL2.

**Outcome.** Better than the milestone asked for: it boots past the title into the attract
demo, loading scenes `0x51` and `0x17` and drawing textured geometry (verified by
screenshot, not inference — an early flat-gradient frame was a transition fade, not proof
of rendering). Audio initialises, `InitOTR` completes, no crashes, no WebGL errors.

Two fixes were needed:

- **The `oot.o2r` preload was silently dropped.** CMake de-duplicates repeated identical
  link-option tokens and `--preload-file` appeared twice, so only the first archive
  survived. `SHELL:` keeps each flag and its path together.
- **Browser logging dominated boot time.** One boot emitted 7,670 console lines, 7,479 of
  them ResourceManager traces; each spdlog call crosses into JS and lands in devtools.
  Compiling trace/debug out took the same boot to 28 lines. Note this means the "is the
  synchronous ResourceManager fast enough" question was never really tested before — it is
  still unanswered, now without the logging confound.

**Release build measured (2026-09-12), and it settles two open questions.**

| | Debug | Release |
|---|---|---|
| `soh.wasm` | 604 MB | **24.7 MB** |
| gzip -9 | — | 6.5 MB |
| **brotli -q 11** | — | **4.2 MB** |

4.2 MB over the wire is an ordinary web download, so **size is not a constraint** and no
splitting or streaming strategy is needed. The 37.7 MB of archives need not be downloaded
at all under Prelude, which already holds them in IndexedDB on the same origin.

And the ~25s from init to the first attract-demo scene is **not** slowness, as an earlier
draft of this section claimed. Debug measures 25s and Release 24s for the same interval —
if it were CPU-bound, `-O2` would have collapsed it. It is the N64 logo plus title-screen
idle before the demo starts, i.e. the game's own timing. The Release build renders the
title screen correctly and is responsive to keyboard input (confirmed by hand).

The synchronous-ResourceManager question is therefore still genuinely open, but there is no
longer any evidence pointing at it.

Known rough edges in `libultraship/src/fast/backends/gfx_opengl.cpp`:

- **No GL context version is requested** outside `__APPLE__` (`gfx_sdl2.cpp:340-345`), so
  Emscripten's SDL hands back a WebGL1 context. Set `SDL_GL_CONTEXT_MAJOR_VERSION=3` plus
  the ES profile under `__EMSCRIPTEN__`, and `-sMAX_WEBGL_VERSION=2`.
- `glewInit()` (`:677-679`) is guarded `#if !defined(__linux__) && !defined(__OpenBSD__)`;
  Emscripten does **not** define `__linux__`, so it compiles in. Same trap in
  `Context::GetAppBundlePath` (`Context.cpp:414`, `/proc/self/exe`) and
  `CrashHandler.cpp:75,419`.
- Depth readback is compiled out under `USE_OPENGLES` (`:961-964`, `:1001-1003`), leaving
  `depth_stencil_value` uninitialized. Consumers: `z_kankyo.c:232-242` (sun / lens flare
  occlusion) and `z_lights.c:357-399`. Needs a depth-to-color path or a CVar to disable.
- `ReadFramebufferToCPU` (`:944`) uses `GL_UNSIGNED_SHORT_5_5_5_1`; WebGL2 `readPixels`
  permits only RGBA/UNSIGNED_BYTE. Called from `interpreter.cpp:3592`.
- `GL_MIRROR_CLAMP_TO_EDGE` (`:560`) does not exist in WebGL2 — INVALID_ENUM, and
  mirrored-clamp textures wrap wrong (already true on GLES).
- MSAA resolves a `GL_RGB8` renderbuffer (`:778`) to a `GL_RGB8` texture (`:773`); the
  formats match, but WebGL2 enforces the resolve rule strictly. Verify, or default MSAA
  to 1.
- ImGui: `ImGuiConfigFlags_ViewportsEnable` is on by default (`Gui.cpp:127-129`) and needs
  real OS windows — force it off. `IMGUI_IMPL_OPENGL_ES3` is defined only in
  `linux.cmake:7`; the Emscripten arm needs it too.

### Milestone 4 — playable

o2r plus mod layer read from IndexedDB at boot; keyboard input; save persistence.

**IDBFS is write-behind** — nothing reaches IndexedDB until `FS.syncfs()`. Hooks are needed
after `SaveFileThreaded` (`SaveManager.cpp:1200`) and `Config::Save`
(`ConsoleVariable.cpp:243,276`), plus a `beforeunload` / visibility hook, because the
config save in `~Context` (`Context.cpp:45`) never runs in a browser tab. The mount and its
initial `syncfs(true)` are async and must finish in `preRun` before `main`. MEMFS also
holds a JS-side copy of the archive, so budget its size twice.

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
- ImGui (~45 files) renders fine on WebGL2 and is the **only debugging surface** available
  in a browser tab, where no native debugger can attach. The console commands
  (`soh/soh/Enhancements/debugconsole.cpp`) are worth more here than on desktop, not less.

If something in that layer blocks boot, disable it **at runtime via CVar defaults** —
reversible, no deletion. The concrete starting list: `MultiViewports=0`,
`InterpolationFPS=20`, MSAA 1, and the remote-control CVars off. The amputations in
Milestone 1 are a different axis: they target OS services that do not exist in a browser,
not UI surface area.

## Other traps found while surveying

- **The fault path is silent in wasm.** `Fault_AddHungupAndCrashImpl`
  (`soh/src/code/fault.c:1099-1107`) crashes deliberately via `*(u32*)0x11111111 = 0`. In
  linear memory that is a *valid* address ~286 MB in — no trap, execution continues with
  corrupted state. Replace with `abort()` under `__EMSCRIPTEN__`.
- **Indirect-call signature traps.** wasm traps when a call's declared and actual
  signatures differ; the decomp does this freely and native builds tolerate it. Example:
  `Fault_AddClient(&sGraphFaultClient, Graph_FaultClient, 0, 0)` (`graph.c:161`) passes a
  zero-argument function (`graph.c:47`) through a `void*` callback slot. Enumerate these
  with a native `-fsanitize=function` build *before* chasing them in a browser.
- **Real-time audio on the main thread.** Audio is synthesized only when a frame runs, and
  scene/object loads block (`ResourceManager.cpp:218,337` `.get()`; the boot-time
  `ResourceMgr_LoadDirectory("audio")` at `OTRGlobals.cpp:1051`). Any long frame starves
  `SDL_QueueAudio` and crackles. WebAudio contexts also start suspended until a user
  gesture — verify SDL's Emscripten backend resumes on first input, or add JS glue.

Confirmed *not* to be problems: `PadMgr_ThreadEntry` (`padmgr.c:422` — non-blocking recv
then `break`), `Sleep_Msec` (`sleep.c:10`, returns immediately), the `OS_MESG_BLOCK` call
sites (all hit LUS's non-blocking stubs), `IsFrameReady()` (`gfx_sdl2.cpp:635`, always
true), and setjmp/longjmp (none in the tree).

## Open questions

- Is `-fwasm-exceptions` acceptable for the target browsers?
- Memory ceiling. wasm32 caps at 4 GB and `-sMAXIMUM_MEMORY=4GB` works in current desktop
  browsers, but Safari and iOS are tighter. The ">512 MB" figure is still unmeasured.
  Unbound raises it further (`GBI_S32_VTX`, `GBI_FLOAT_MTX`, uncapped collision, growable
  text) — measure unbound's desktop RSS before committing to the combined target.
- Whether the synchronous ResourceManager executor is fast enough at scene load, or
  whether it visibly hitches.
- Performance generally. Untested.
