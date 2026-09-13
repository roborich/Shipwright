#pragma once

// SOH [WASM] The channel between the browser build and whatever page embeds it. Two
// directions, neither of which knows anything about the embedder:
//
//   out: DOM CustomEvents named "soh" on window, detail = { type, ...fields }.
//        "load-game"  { fileNum }                 a save was loaded; console commands that
//                                                need a play state are now safe
//        "scene"      { sceneNum, entranceIndex } a scene finished initialising
//        "file-saved" { path, bytes }             the game wrote its config or a save;
//                                                bytes is a Uint8Array copy of the file
//        "quit"       {}                          the main loop has stopped for good
//        "error"      { message }                 an exception escaped a frame; the loop
//                                                stops after this too
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
// Called by Graph_ProcessGfxCommands with the number of frames it drew this tick; read
// back through Soh_GetStats.
void Soh_EmbedderCountDraws(size_t draws);
#ifdef __cplusplus
}
#endif
#endif
