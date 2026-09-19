// The oot.o2r -> oot-unbound.o2r converter (soh-unbound-convert.js), checked in-process for what it
// produces and in a browser module worker for the contract a host page sees (HOST-API.md §7). The ROM
// chain -- soh-extract then this converter, in one worker -- needs SOH_WASM_ROM.

import { afterAll, beforeAll, expect, test } from "bun:test";
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { basename, dirname, join } from "node:path";
import { pathToFileURL } from "node:url";
import type { Browser } from "playwright-core";
import { BUILD_DIR, FILES_DIR, TEST_TIMEOUT } from "./lib/env";
import { launchBrowser, openPage, withTimeout } from "./lib/game";
import { startServer, type TestServer } from "./lib/server";
import { defaultMounts } from "./lib/suite";
import { zipIndex } from "./lib/zip";

const ROM = process.env.SOH_WASM_ROM;
const MODULE = join(BUILD_DIR, "soh-unbound-convert.js");
const ARTIFACTS = ["soh-unbound-convert.js", "soh-unbound-convert.wasm"];
const OOT = join(FILES_DIR, "oot.o2r");
// A base the game (or --export-unbound) converted from the same oot.o2r, when the files directory has one.
const REFERENCE_BASE = join(FILES_DIR, "oot-unbound.o2r");
const MB = 1024 * 1024;

const sha256 = (bytes: Uint8Array) => createHash("sha256").update(bytes).digest("hex");

async function createConverter() {
    const { default: create } = await import(pathToFileURL(MODULE).href);
    return create({ locateFile: (name: string) => join(BUILD_DIR, name), print() {}, printErr() {} });
}

async function conversionError(bytes: Uint8Array): Promise<any> {
    const mod = await createConverter();
    return mod.convertToUnbound(bytes).catch((e: Error) => e);
}

test("the Unbound converter ships next to soh.js", () => {
    const missing = ARTIFACTS.filter((name) => !existsSync(join(BUILD_DIR, name)));
    expect(missing).toEqual([]);
});

test("in-process: oot.o2r becomes the base the game itself converts", async () => {
    const mod = await createConverter();
    const { bytes, report } = await mod.convertToUnbound(new Uint8Array(readFileSync(OOT)));
    expect(report).toMatchObject({ code: 0, error: "", failures: 0 });
    expect(report.converter).toMatch(/^soh .+ unbound r\d+$/);
    expect(report.scenes).toBeGreaterThan(100);
    const entries = zipIndex(bytes);
    expect(entries.get("unbound.json")).toBeDefined();
    expect(entries.get("version")).toBeDefined();
    expect(entries.get("portVersion")).toBeDefined();
    if (existsSync(REFERENCE_BASE)) {
        // The converter is deterministic, so the same source and the same code give the same bytes.
        expect(sha256(bytes)).toBe(sha256(new Uint8Array(readFileSync(REFERENCE_BASE))));
    } else {
        console.log(`no oot-unbound.o2r in ${FILES_DIR}; byte comparison skipped`);
    }
}, TEST_TIMEOUT);

test("in-process: what is not an oot.o2r is refused with a reason", async () => {
    const garbage = await conversionError(new Uint8Array(1000).fill(7));
    expect(garbage).toBeInstanceOf(Error);
    expect(garbage.code).toBe(-2);
    expect(garbage.message).toContain("not an OoT game archive");

    if (existsSync(REFERENCE_BASE)) {
        const base = await conversionError(new Uint8Array(readFileSync(REFERENCE_BASE)));
        expect(base.code).toBe(-2);
        expect(base.message).toContain("already an Unbound base");
    }
}, TEST_TIMEOUT);

test("in-process: an instance converts once", async () => {
    const mod = await createConverter();
    await mod.convertToUnbound(new Uint8Array(100)).catch(() => {});
    const error = await mod.convertToUnbound(new Uint8Array(100)).catch((e: Error) => e);
    expect(error.message).toContain("already run");
    expect(error.code).toBe(-1);
}, TEST_TIMEOUT);

// In a browser: both modules run in a module worker off the main thread, and only the report and a
// digest come back through postMessage.
let browser: Browser;
let server: TestServer;

beforeAll(async () => {
    server = startServer({ ...defaultMounts(), ...(ROM ? { "/rom/": dirname(ROM) } : {}) });
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

async function prepareInWorker(source: Record<string, string>): Promise<any> {
    const page = await openPage(browser, `${server.url}/page/unbound.html`);
    try {
        return await page.page.evaluate((s) => (window as any).__unbound(s), source);
    } finally {
        await page.close();
    }
}

test("in a module worker: oot.o2r becomes the same base as in-process", async () => {
    const result = await prepareInWorker({ o2rUrl: "/files/oot.o2r" });
    expect(result.report).toMatchObject({ code: 0, failures: 0 });
    expect(result.byteLength).toBeGreaterThan(20 * MB);
    if (existsSync(REFERENCE_BASE)) {
        expect(result.sha256).toBe(sha256(new Uint8Array(readFileSync(REFERENCE_BASE))));
    }
    console.log(`worker converted oot.o2r in ${result.seconds.toFixed(1)}s`);
}, TEST_TIMEOUT);

test.skipIf(!ROM)(
    "in a module worker: a ROM becomes an Unbound base (soh-extract, then the converter)",
    async () => {
        const result = await prepareInWorker({ romUrl: `/rom/${encodeURIComponent(basename(ROM!))}` });
        expect(result.report).toMatchObject({ code: 0, failures: 0 });
        expect(result.report.scenes).toBeGreaterThan(100);
        expect(result.byteLength).toBeGreaterThan(20 * MB);
        console.log(`worker turned the ROM into oot-unbound.o2r in ${result.seconds.toFixed(1)}s`);
    },
    TEST_TIMEOUT,
);
