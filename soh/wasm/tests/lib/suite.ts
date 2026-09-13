import { afterAll, beforeAll } from "bun:test";
import type { Browser, Page } from "playwright-core";
import { BUILD_DIR, FILES_DIR, TESTS_DIR, requireBuild } from "./env";
import { bootGame, launchBrowser, openPage, type BootOptions, type Game } from "./game";
import { startServer, type Mounts, type TestServer } from "./server";
import { join } from "node:path";

export type Suite = { browser: Browser; server: TestServer };

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
        await suite.browser?.close();
        suite.server?.stop();
    });
    return suite;
}

export async function withGame(suite: Suite, options: BootOptions, body: (game: Game) => Promise<void>): Promise<void> {
    const game = await bootGame(suite.browser, suite.server, options);
    try {
        await body(game);
    } finally {
        await game.close();
    }
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
