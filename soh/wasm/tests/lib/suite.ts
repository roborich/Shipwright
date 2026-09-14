import { afterAll, beforeAll } from "bun:test";
import type { Browser, Page } from "playwright-core";
import { BUILD_DIR, FILES_DIR, TESTS_DIR, requireBuild } from "./env";
import { bootGame, launchBrowser, openPage, withTimeout, type BootOptions, type Game } from "./game";
import { startServer, type Mounts, type TestServer } from "./server";
import { join } from "node:path";

export type Suite = { browser: Browser; server: TestServer };

const BROWSER_CLOSE_TIMEOUT = 15_000;

export function defaultMounts(): Mounts {
    return { "/build/": BUILD_DIR, "/files/": FILES_DIR, "/page/": join(TESTS_DIR, "page") };
}

// One server and one browser per test file; each test gets its own browser context.
export function useSuite(jsFlags: string[] = []): Suite {
    const suite = {} as Suite;
    beforeAll(async () => {
        requireBuild();
        suite.server = startServer(defaultMounts());
        suite.browser = await launchBrowser(jsFlags);
    });
    afterAll(async () => {
        // bun applies no timeout to hooks, and browser.close() never settles when a page's
        // renderer is wedged in wasm (the main thread never returns to the event loop, so the
        // browser never finishes closing it). Left unbounded, that kept `bun test` alive for
        // hours. Bound it, and kill the browser process outright if closing does not finish.
        try {
            if (suite.browser) {
                await withTimeout(suite.browser.close(), BROWSER_CLOSE_TIMEOUT, "closing the browser").catch(() => {
                    suite.browser.process()?.kill("SIGKILL");
                });
            }
        } finally {
            suite.server?.stop();
        }
    });
    return suite;
}

export async function withGame(suite: Suite, options: BootOptions, body: (game: Game) => Promise<void>): Promise<void> {
    const game = await bootGame(suite.browser, suite.server, options);
    try {
        await body(game);
    } catch (error) {
        // The page's console arrives over the protocol, not through evaluate, so it is
        // readable even when the main thread is stuck; it is usually the only clue. (Attaching
        // the debugger to read the stack is not an option: that needs the main thread too.)
        throw withPageConsole(error, game);
    } finally {
        await game.close();
    }
}

function withPageConsole(error: unknown, game: Game): unknown {
    const lines = [...game.pageErrors, ...game.consoleErrors].slice(-8);
    if (error instanceof Error && lines.length) {
        error.message += `\n  page console (last ${lines.length}):\n    ${lines.join("\n    ")}`;
    }
    return error;
}

export async function withPage(
    suite: Suite,
    path: string,
    setup: ((page: Page) => Promise<void>) | undefined,
    body: (game: Game) => Promise<void>,
): Promise<void> {
    const game = await openPage(suite.browser, `${suite.server.url}${path}`, setup);
    try {
        await body(game);
    } finally {
        await game.close();
    }
}
