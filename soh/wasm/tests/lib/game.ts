import { chromium, type Browser, type Page } from "playwright-core";
import { HEADED } from "./env";
import type { TestServer } from "./server";
import type { StatsSample } from "./timing";

// Globals that exist only inside the page; the callbacks passed to page.evaluate run there.
declare const Module: any;
declare global {
    interface Window {
        __soh: Recorder;
        __sohBoot: BootConfig;
    }
}

export type SohEvent = { type: string; at: number; [field: string]: any };
type Recorder = {
    events: SohEvent[];
    savedBytes: Record<string, Uint8Array>;
    listenerResults: number[];
    runtimeReady: boolean;
    aborted: string | null;
    harnessError: string | null;
};
type BootConfig = { files: Record<string, string>; inlineFiles: Record<string, string>; moduleUrl: string };

export type BootOptions = {
    // VFS path -> path under the files mount.
    files?: Record<string, string>;
    // VFS path -> text, for configs a test builds itself.
    inlineFiles?: Record<string, string>;
};

// N64 buttons on the keyboard, as the files directory's config maps them.
export const PAD = { A: "KeyX", B: "KeyC", START: "Space" } as const;

export const DEFAULT_FILES: Record<string, string> = {
    "/oot.o2r": "oot.o2r",
    "/shipofharkinian.json": "shipofharkinian.json",
    "/Save/file1.sav": "Save/file1.sav",
    "/Save/global.sav": "Save/global.sav",
};

const FATAL_CONSOLE = /Unhandled exception|Aborted\(|RuntimeError|Uncaught/;

// A page whose main thread is stuck (a wasm loop that never returns to the event loop) never
// answers page.evaluate. Without a bound, a test waits on it past its own deadline, bun's test
// timeout abandons the promise, and the process never exits.
const PAGE_CALL_TIMEOUT = 10_000;

export function withTimeout<T>(promise: Promise<T>, ms: number, what: string): Promise<T> {
    let timer: ReturnType<typeof setTimeout>;
    const timeout = new Promise<never>((_, reject) => {
        timer = setTimeout(() => reject(new Error(`${what} did not finish within ${ms} ms`)), ms);
    });
    return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

export function withoutFile(files: Record<string, string>, vfsPath: string): Record<string, string> {
    return Object.fromEntries(Object.entries(files).filter(([path]) => path !== vfsPath));
}

export function describeEvents(events: SohEvent[]): string {
    const brief = events.map((e) =>
        e.type === "scene" ? `scene(${e.sceneNum}/0x${e.entranceIndex.toString(16)})` : e.path ? `${e.type}(${e.path})` : e.type,
    );
    return brief.length ? brief.slice(-20).join(", ") : "none";
}

export async function launchBrowser(jsFlags: string[] = []): Promise<Browser> {
    const args = ["--autoplay-policy=no-user-gesture-required"];
    if (jsFlags.length) {
        args.push(`--js-flags=${jsFlags.join(" ")}`);
    }
    return chromium.launch({ headless: !HEADED, args });
}

// Runs in the page before any of its own scripts, so no event is missed.
function installRecorder() {
    const record: any = {
        events: [],
        savedBytes: {},
        listenerResults: [],
        runtimeReady: false,
        aborted: null,
        harnessError: null,
    };
    window.__soh = record;
    window.addEventListener("soh", (e: any) => {
        const { bytes, ...fields } = e.detail;
        if (bytes) {
            record.savedBytes[fields.path] = bytes;
            fields.byteLength = bytes.length;
        }
        record.events.push({ ...fields, at: performance.now() });
    });
}

// Opens any page (host.html, index.html, the harness) with the event recorder installed.
export async function openPage(browser: Browser, url: string, setup?: (page: Page) => Promise<void>): Promise<Game> {
    const context = await browser.newContext({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 2 });
    const page = await context.newPage();
    const game = new Game(page);
    await page.addInitScript(installRecorder);
    if (setup) {
        await setup(page);
    }
    await page.goto(url);
    return game;
}

// Boots the build through page/harness.html, the way an embedder hands over its files.
export async function bootGame(browser: Browser, server: TestServer, options: BootOptions = {}): Promise<Game> {
    const boot: BootConfig = {
        files: Object.fromEntries(Object.entries(options.files ?? DEFAULT_FILES).map(([vfs, rel]) => [vfs, `/files/${rel}`])),
        inlineFiles: options.inlineFiles ?? {},
        moduleUrl: "/build/soh.js",
    };
    const game = await openPage(browser, `${server.url}/page/harness.html`, (page) =>
        page.addInitScript((config) => {
            window.__sohBoot = config;
        }, boot),
    );
    await game.waitUntil(() => game.evaluate(() => window.__soh.runtimeReady), "the runtime to start", 60_000);
    return game;
}

export class Game {
    crashed = false;
    readonly consoleErrors: string[] = [];
    readonly pageErrors: string[] = [];

    constructor(readonly page: Page) {
        page.on("crash", () => (this.crashed = true));
        page.on("pageerror", (error) => this.pageErrors.push(String(error)));
        page.on("console", (message) => {
            if (message.type() === "error") {
                this.consoleErrors.push(message.text());
            }
        });
    }

    async close(): Promise<void> {
        await withTimeout(this.page.context().close(), PAGE_CALL_TIMEOUT, "closing the page").catch(() => {});
    }

    evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg?: A): Promise<R> {
        return withTimeout(
            this.page.evaluate(fn as any, arg) as Promise<R>,
            PAGE_CALL_TIMEOUT,
            "a call into the page (is its main thread stuck?)",
        );
    }

    // ---- events ---------------------------------------------------------------------------

    events(): Promise<SohEvent[]> {
        return this.evaluate(() => window.__soh.events);
    }

    async eventCount(): Promise<number> {
        return (await this.events()).length;
    }

    // Waits for an event of `type` at or after index `from` that satisfies `match`.
    async waitForEvent(
        type: string,
        { from = 0, match = () => true, timeout = 60_000 }: { from?: number; match?: (e: SohEvent) => boolean; timeout?: number } = {},
    ): Promise<{ event: SohEvent; index: number }> {
        return this.waitUntil(
            async () => {
                const events = await this.events();
                const index = events.findIndex((e, i) => i >= from && e.type === type && match(e));
                return index >= 0 ? { event: events[index], index } : null;
            },
            `a '${type}' event`,
            timeout,
        );
    }

    async waitUntil<T>(probe: () => Promise<T | null | undefined | false>, what: string, timeout: number): Promise<T> {
        const deadline = performance.now() + timeout;
        for (;;) {
            await this.assertAlive();
            const value = await probe();
            if (value) {
                return value;
            }
            if (performance.now() > deadline) {
                throw new Error(`timed out after ${timeout} ms waiting for ${what}; events: ${describeEvents(await this.events())}`);
            }
            await Bun.sleep(200);
        }
    }

    async assertAlive(): Promise<void> {
        if (this.crashed) {
            throw new Error("the renderer crashed");
        }
        const state = await this.evaluate(() => ({ aborted: window.__soh.aborted, harnessError: window.__soh.harnessError }));
        if (state.aborted || state.harnessError) {
            throw new Error(`the module stopped: ${state.aborted ?? state.harnessError}`);
        }
    }

    // Everything that suggests the game is broken: error events, fatal console lines,
    // uncaught page errors.
    async problems(): Promise<string[]> {
        const errorEvents = (await this.events()).filter((e) => e.type === "error").map((e) => `error event: ${e.message}`);
        return [...errorEvents, ...this.consoleErrors.filter((t) => FATAL_CONSOLE.test(t)), ...this.pageErrors];
    }

    // ---- commands -------------------------------------------------------------------------

    run(command: string): Promise<number> {
        return this.evaluate((c) => Module.ccall("Soh_RunConsoleCommand", "number", ["string"], [c]), command);
    }

    // Adds a listener that runs `command` the first time a `type` event arrives, from inside
    // the event dispatch, and records the command's result in __soh.listenerResults.
    armCommandOnEvent(type: string, command: string): Promise<void> {
        return this.evaluate(
            ([t, c]) => {
                const listener = (e: any) => {
                    if (e.detail.type !== t) return;
                    window.removeEventListener("soh", listener);
                    window.__soh.listenerResults.push(Module.ccall("Soh_RunConsoleCommand", "number", ["string"], [c]));
                };
                window.addEventListener("soh", listener);
            },
            [type, command],
        );
    }

    listenerResults(): Promise<number[]> {
        return this.evaluate(() => window.__soh.listenerResults);
    }

    // ---- diagnostics ----------------------------------------------------------------------

    stats(): Promise<StatsSample & { sceneNum: number }> {
        return this.evaluate(() => ({ ...JSON.parse(Module.ccall("Soh_GetStats", "string", [], [])), t: performance.now() }));
    }

    async sampleStats(durationMs: number, everyMs = 250): Promise<StatsSample[]> {
        const samples: StatsSample[] = [];
        const end = performance.now() + durationMs;
        while (performance.now() < end) {
            await this.assertAlive();
            samples.push(await this.stats());
            await Bun.sleep(everyMs);
        }
        return samples;
    }

    heapBytes(): Promise<number> {
        return this.evaluate(() => Module.HEAPU8.buffer.byteLength);
    }

    // ---- filesystem -----------------------------------------------------------------------

    listDir(path: string): Promise<string[]> {
        return this.evaluate((p) => (Module.FS.analyzePath(p).exists ? Module.FS.readdir(p).filter((n: string) => n !== "." && n !== "..") : []), path);
    }

    writeFile(path: string, text: string): Promise<void> {
        return this.evaluate(([p, t]) => Module.FS.writeFile(p, t), [path, text]);
    }

    unlink(path: string): Promise<void> {
        return this.evaluate((p) => Module.FS.unlink(p), path);
    }

    // ---- input and picture ----------------------------------------------------------------

    // Holds a key long enough for a 20 Hz frame loop to see it.
    async pressButton(key: string, holdMs = 200): Promise<void> {
        await this.page.locator("#canvas").focus();
        await this.page.keyboard.down(key);
        await Bun.sleep(holdMs);
        await this.page.keyboard.up(key);
    }

    // Fraction of sampled canvas pixels that are not near-black.
    async canvasLitFraction(): Promise<number> {
        const png = await this.page.locator("#canvas").screenshot({ timeout: PAGE_CALL_TIMEOUT });
        return this.evaluate(async (b64) => {
            const bitmap = await createImageBitmap(await (await fetch(`data:image/png;base64,${b64}`)).blob());
            const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
            const context = canvas.getContext("2d")!;
            context.drawImage(bitmap, 0, 0);
            const data = context.getImageData(0, 0, bitmap.width, bitmap.height).data;
            let lit = 0;
            let sampled = 0;
            for (let i = 0; i < data.length; i += 4 * 7) {
                sampled++;
                if (data[i] + data[i + 1] + data[i + 2] > 30) lit++;
            }
            return lit / sampled;
        }, png.toString("base64"));
    }

    // The title fades in and out, so a single screenshot can be black; keep looking.
    waitForPicture(minLit = 0.05, timeout = 30_000): Promise<number> {
        return this.waitUntil(async () => {
            const lit = await this.canvasLitFraction();
            return lit >= minLit ? lit : null;
        }, `the canvas to show at least ${minLit * 100}% lit pixels`, timeout);
    }
}

// Title -> file select -> file 1 -> "Yes". Resolves once the save has loaded.
export async function loadFirstSave(game: Game): Promise<{ event: SohEvent; index: number }> {
    const from = await game.eventCount();
    if ((await game.run("file_select")) !== 0) {
        throw new Error("file_select refused");
    }
    await Bun.sleep(3000);
    await game.pressButton(PAD.A);
    await Bun.sleep(1500);
    await game.pressButton(PAD.A);
    return game.waitForEvent("load-game", { from, timeout: 30_000 });
}
