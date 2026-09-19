#pragma once

// SOH [WASM] The channel between the browser build and whatever page embeds it. Two
// directions, neither of which knows anything about the embedder:
//
//   out: DOM CustomEvents named "soh" on window, detail = { type, ...fields }, dispatched
//        between frames so a listener may send commands back.
//        "load-game"    { fileNum }                 a save was loaded and its first scene is
//                                                  up; commands that need a play state work
//        "scene"        { sceneNum, entranceIndex } a scene finished initialising
//        "file-saved"   { path, bytes }             the game wrote its config, a save or the
//                                                  Unbound base archive; bytes is a
//                                                  Uint8Array copy of the file
//        "file-removed" { path }                    a watched file is gone
//        "quit"         {}                          the main loop has stopped for good
//        "error"        { message }                 the game stopped: an exception escaped
//                                                  a frame, or the archives are unusable
//   in:  Module.ccall('Soh_RunConsoleCommand', 'number', ['string'], ['entrance cd'])
//        runs a line through the game's debug console; returns the command's result.
//   diagnostics: Module.ccall('Soh_GetStats', 'string') returns counters as JSON, for tests.
//
// See soh/wasm/EMBEDDING.md, "Talking to the game".
#ifdef __EMSCRIPTEN__
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void Soh_InitEmbedderBridge(void);
// Called by the frame loop (graph.c) after each frame: reports files the game wrote.
void Soh_EmbedderAfterFrame(void);
// Called by the frame loop when the window has closed, before the loop is cancelled.
void Soh_EmbedderQuit(void);
// Called by the frame guard for an exception escaping a frame.
void Soh_EmbedderError(const char* message);
// Reports a file the game wrote before the bridge started watching, as "file-saved": the
// Unbound base archive, which is converted during startup.
void Soh_EmbedderReportFile(const char* path);
// Called by Graph_ProcessGfxCommands with the number of frames it drew this tick; read
// back through Soh_GetStats.
void Soh_EmbedderCountDraws(size_t draws);
// Called by the audio update when the SDL player is about to drop it; read back through
// Soh_GetStats.
void Soh_EmbedderCountAudioDrop(void);
#ifdef __cplusplus
}
#endif
#endif
