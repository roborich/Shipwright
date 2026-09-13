import { expect } from "bun:test";
import { baseConfig } from "./lib/configs";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { knownBug } from "./lib/known";
import { useSuite, withGame } from "./lib/suite";
import { audioDrains } from "./lib/timing";

const suite = useSuite();

// A4: a config from a Mac install names "coreaudio". The browser build has only the SDL
// player, and LUS fell through to the null player for any backend it was not built with, so
// the game ran silent with nothing in the console.
knownBug("A4", "a config naming a backend this build lacks still plays audio", () => {
    const config = baseConfig();
    config.Window = { ...config.Window, AudioBackend: "coreaudio" };
    return withGame(suite, { inlineFiles: { "/shipofharkinian.json": JSON.stringify(config) } }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        const samples = await game.sampleStats(5000, 100);
        expect(audioDrains(samples)).toBe(true);
    });
}, TEST_TIMEOUT);
