import { expect, test } from "bun:test";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { BUILD_DIR } from "./lib/env";
import { knownBug } from "./lib/known";
import { exportNames, largestFunctions } from "./lib/wasm";

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

knownBug("B5", "the module exports only what the page needs", async () => {
    const names = exportNames(wasm());
    expect(names).toContain("Soh_RunConsoleCommand");
    expect(names.length).toBeLessThan(150);
}, 60_000);
