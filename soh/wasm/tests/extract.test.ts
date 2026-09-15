// The ROM -> o2r converter (soh-extract.js), checked two ways: in-process for what it
// produces, and in a browser module worker for the contract a host page sees.
// Needs SOH_WASM_ROM pointing at a ROM; without it only the artifact checks run.

import { afterAll, beforeAll, expect, test } from "bun:test";
import { existsSync, readFileSync } from "node:fs";
import { basename, dirname, join } from "node:path";
import { pathToFileURL } from "node:url";
import type { Browser } from "playwright-core";
import { BUILD_DIR, FILES_DIR, TEST_TIMEOUT } from "./lib/env";
import { launchBrowser, openPage, withTimeout } from "./lib/game";
import { startServer, type TestServer } from "./lib/server";
import { defaultMounts } from "./lib/suite";
import { zipDifferences, zipIndex } from "./lib/zip";

const ROM = process.env.SOH_WASM_ROM;
const MODULE = join(BUILD_DIR, "soh-extract.js");
const ARTIFACTS = ["soh-extract.js", "soh-extract.wasm"];
const MB = 1024 * 1024;

test("the converter ships next to soh.js", () => {
    const missing = ARTIFACTS.filter((name) => !existsSync(join(BUILD_DIR, name)));
    expect(missing).toEqual([]);
});

async function createExtractor() {
    const { default: createSohExtractor } = await import(pathToFileURL(MODULE).href);
    return createSohExtractor({ locateFile: (name: string) => join(BUILD_DIR, name), print() {}, printErr() {} });
}

test.skipIf(!ROM)(
    "in-process: a ROM becomes an archive with the entries a desktop extraction produces",
    async () => {
        const mod = await createExtractor();
        const progress: Progress[] = [];
        const result = await mod.extractRom(new Uint8Array(readFileSync(ROM!)), {
            quiet: true,
            onProgress: (done: number, total: number, info: ProgressInfo) => progress.push([done, total, info]),
        });

        expect(["oot.o2r", "oot-mq.o2r"]).toContain(result.name);
        expect(result.version).not.toBe("");
        expect(result.bytes.length).toBeGreaterThan(20 * MB);
        const produced = zipIndex(result.bytes);
        expect(produced.get("version")).toBeDefined();
        expect(produced.get("portVersion")).toBeDefined();
        expectTwoPhases(progress, produced.size);

        // The desktop-extracted archive the other tests boot from is the reference, when it
        // came from the same ROM version (its `version` entry holds the ROM CRC).
        const referencePath = join(FILES_DIR, result.name);
        const reference = existsSync(referencePath) ? zipIndex(new Uint8Array(readFileSync(referencePath))) : null;
        if (reference && reference.get("version")?.crc === produced.get("version")?.crc) {
            expect(zipDifferences(produced, reference)).toEqual([]);
        } else {
            console.log(`no desktop archive from the same ROM version in ${FILES_DIR}; byte comparison skipped`);
        }
    },
    TEST_TIMEOUT,
);

type ProgressInfo = { phase: "recipe" | "write"; file?: string };
type Progress = [number, number, ProgressInfo];

// Every recipe file in order, each named, then the archive's entries as they are written.
function expectTwoPhases(progress: Progress[], entries: number): void {
    const firstWrite = progress.findIndex(([, , info]) => info.phase === "write");
    expect(firstWrite).toBeGreaterThan(100);
    const recipe = progress.slice(0, firstWrite);
    const write = progress.slice(firstWrite);

    expect(recipe.every(([, , info]) => info.phase === "recipe")).toBe(true);
    expect(recipe.map(([done]) => done)).toEqual(recipe.map((_, i) => i + 1));
    const total = recipe[0][1];
    expect(recipe.at(-1)![0]).toBe(total);
    expect(recipe.every(([, , info]) => info.file?.startsWith("assets/xml/") && info.file.endsWith(".xml"))).toBe(true);

    expect(write.every(([, , info]) => info.phase === "write")).toBe(true);
    expect(write.length).toBeGreaterThan(10);
    expect(write[0]).toEqual([0, entries, { phase: "write" }]);
    expect(write.at(-1)).toEqual([entries, entries, { phase: "write" }]);
    const dones = write.map(([done]) => done);
    expect(dones).toEqual([...dones].sort((a, b) => a - b));
}

test("in-process: a file that is not a ROM is refused with the desktop's reason", async () => {
    const mod = await createExtractor();
    const error = await mod.extractRom(new Uint8Array(32 * MB), { quiet: true }).catch((e: Error) => e);
    expect(error).toBeInstanceOf(Error);
    expect((error as any).code).toBe(-4);
    expect(error.message).toContain("not one this build can extract");
}, TEST_TIMEOUT);

// ZAPD's own failures are caught on the C++ side, but a trap (bad data indexing past a
// buffer) escapes the call as something that is not an Error. The wrapper still owes the
// host an Error with a code, the stderr ZAPD left behind, and a clean VFS. A real ROM cannot
// drive this path (it would fail the CRC check first), so the call itself is stubbed.
test("in-process: a failure that escapes the wasm still rejects with an Error and a code", async () => {
    const mod = await createExtractor();
    mod.ccall = (name: string) => {
        if (name !== "Extract_RomToO2r") throw new Error(`unexpected ccall ${name}`);
        mod.printErr("error: something ZAPD said");
        throw { toString: () => "[object WebAssembly.Exception]" }; // no .message, no .code
    };
    const error = await mod.extractRom(new Uint8Array(32 * MB), { quiet: true }).catch((e: Error) => e);
    expect(error).toBeInstanceOf(Error);
    expect((error as any).code).toBe(-6);
    expect(error.message).toContain("Extraction failed");
    expect(error.message).toContain("something ZAPD said");
    expect(mod.FS.analyzePath("/rom/rom.z64").exists).toBe(false);
}, TEST_TIMEOUT);

test("in-process: an instance converts once", async () => {
    const mod = await createExtractor();
    await mod.extractRom(new Uint8Array(100), { quiet: true }).catch(() => {});
    const error = await mod.extractRom(new Uint8Array(100), { quiet: true }).catch((e: Error) => e);
    expect(error.message).toContain("already run");
}, TEST_TIMEOUT);

// In a browser: the module runs in a module worker off the main thread, the page never
// touches the VFS, and everything the host needs comes back through postMessage.
let browser: Browser;
let server: TestServer;

beforeAll(async () => {
    if (!ROM) return;
    server = startServer({ ...defaultMounts(), "/rom/": dirname(ROM) });
    browser = await launchBrowser();
});

afterAll(async () => {
    // Bounded for the same reason as lib/suite.ts: a wedged renderer never finishes closing.
    if (browser) {
        await withTimeout(browser.close(), 15_000, "closing the browser").catch(() => {
            browser.process()?.kill("SIGKILL");
        });
    }
    server?.stop();
});

test.skipIf(!ROM)(
    "in a module worker: the host gets progress, then the archive",
    async () => {
        const page = await openPage(browser, `${server.url}/page/extract.html`);
        try {
            const romUrl = `/rom/${encodeURIComponent(basename(ROM!))}`;
            const report = await page.page.evaluate((url) => (window as any).__extract(url), romUrl);
            expect(["oot.o2r", "oot-mq.o2r"]).toContain(report.name);
            expect(report.byteLength).toBeGreaterThan(20 * MB);
            const entries = report.progress.at(-1)[1];
            expectTwoPhases(report.progress, entries);
            console.log(`worker extracted ${report.name} (${report.version}) in ${report.seconds.toFixed(1)}s`);
        } finally {
            await page.close();
        }
    },
    TEST_TIMEOUT,
);
