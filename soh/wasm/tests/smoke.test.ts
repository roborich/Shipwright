import { expect, test } from "bun:test";
import { playConfig, playFiles, staleDesktopConfig } from "./lib/configs";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { PAD } from "./lib/game";
import { useSuite, withGame } from "./lib/suite";

const suite = useSuite();

test("boots to a title screen that renders", () =>
    withGame(suite, {}, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        expect(await game.waitForPicture()).toBeGreaterThan(0.05);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("the frame loop keeps running: reload re-enters the scene", () =>
    withGame(suite, {}, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        const from = await game.eventCount();
        expect(await game.run("reload")).toBe(0);
        await game.waitForEvent("scene", { from, timeout: 30_000 });
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("a stale desktop config opens Settings on the Mod Menu without stopping the game", () =>
    withGame(suite, { inlineFiles: { "/shipofharkinian.json": staleDesktopConfig() } }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        await game.page.locator("#canvas").focus();
        await game.page.keyboard.press("Escape"); // opens the menu bar
        await Bun.sleep(1000);
        await game.page.mouse.click(58, 29); // Settings tab, at 1280x720
        await Bun.sleep(3000);
        expect(await game.problems()).toEqual([]);
        const from = await game.eventCount();
        expect(await game.run("reload")).toBe(0);
        await game.waitForEvent("scene", { from, timeout: 30_000 });
    }), TEST_TIMEOUT);

// Map select's text is GfxPrint: a font and palette compiled into the game, so drawn from raw
// pointers into static data. LUS drops a SETTIMG address at or below 0x0FFFFFFF that no loaded
// module owns as an unresolved N64 segment address; with static data at Emscripten's default
// base that was this font, and the screen came up black (or showed whatever texture was loaded
// last). -sGLOBAL_BASE (soh/CMakeLists.txt) keeps every real pointer above that range.
const debugConfig = () => {
    const config = playConfig();
    config.CVars.gDeveloperTools = { ...config.CVars.gDeveloperTools, DebugEnabled: 1 };
    return config;
};

test("map select draws its text", () =>
    withGame(suite, { inlineFiles: playFiles(debugConfig()) }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        expect(await game.run("file_select")).toBe(0);
        await Bun.sleep(3000);
        await game.pressButton(PAD.A); // file 1
        await Bun.sleep(1500);
        await game.pressButton(PAD.A); // "Yes", which opens map select in debug mode
        await Bun.sleep(5000); // past the fade, so the lit file select cannot pass for it
        expect(await game.waitForPicture(0.02)).toBeGreaterThan(0.02);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);
