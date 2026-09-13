import { expect, test } from "bun:test";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { BUILD_DIR } from "./lib/env";
import { largestFunctions, mostLocals } from "./lib/wasm";

const wasm = () => new Uint8Array(readFileSync(join(BUILD_DIR, "soh.wasm")));

// RegionTable_Init reached 3.3 MB when Binaryen inlined its builders, and V8's optimising
// compiler crashed the renderer on it. See wasm-port.md, "Other traps".
const LARGEST_FUNCTION_BYTES = 1_000_000;

test("HOST-API.md ships next to soh.js", () => {
    expect(existsSync(join(BUILD_DIR, "soh.js"))).toBe(true);
    expect(existsSync(join(BUILD_DIR, "HOST-API.md"))).toBe(true);
});

test("no function is large enough to endanger V8's optimising compiler", () => {
    const tooBig = largestFunctions(wasm(), 5)
        .filter((f) => f.size >= LARGEST_FUNCTION_BYTES)
        .map((f) => `${f.name}: ${f.size} bytes`);
    expect(tooBig).toEqual([]);
});

// Exported functions are never inlined. Without -export-dynamic, Binaryen inlined small
// helpers thousands of times into the randomizer's table builders: HintTable_Init_Exclude_
// Overworld went from 7 locals to 19168, and V8 crashed the renderer compiling it. The
// largest count in a healthy build is a few hundred.
const MOST_LOCALS = 2000;

test("no function declares so many locals that V8 cannot compile it", () => {
    const tooMany = mostLocals(wasm(), 5)
        .filter((f) => f.locals >= MOST_LOCALS)
        .map((f) => `${f.name}: ${f.locals} locals`);
    expect(tooMany).toEqual([]);
});
