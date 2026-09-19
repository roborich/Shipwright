import { expect, test } from "bun:test";
import { staleDesktopConfig } from "./lib/configs";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
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
