import { expect, test } from "bun:test";
import { BAKED_BUILD_DIR, BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { DEFAULT_FILES, withoutFile } from "./lib/game";
import { knownBug } from "./lib/known";
import { useSuite, withGame, withPage } from "./lib/suite";

const suite = useSuite();

knownBug("B1", "a missing oot.o2r is reported as an error event", () =>
    withGame(suite, { files: withoutFile(DEFAULT_FILES, "/oot.o2r") }, async (game) => {
        const { event } = await game.waitForEvent("error", { timeout: BOOT_TIMEOUT });
        expect(event.message).toContain("oot.o2r");
    }), TEST_TIMEOUT);

test("host.html boots with every host file present", () =>
    withPage(suite, "/build/host.html", undefined, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

knownBug("B3", "host.html boots cleanly when an optional save is missing", () =>
    withPage(
        suite,
        "/build/host.html",
        (page) => page.route("**/hostfiles/Save/file1.sav", (route) => route.fulfill({ status: 404, body: "not found" })),
        async (game) => {
            await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
            expect((await game.listDir("/Save")).filter((name) => name.endsWith(".bak"))).toEqual([]);
        },
    ), TEST_TIMEOUT);

if (BAKED_BUILD_DIR) {
    knownBug("B2", "a baked build boots from its own index.html", () =>
        withPage(suite, "/baked/index.html", undefined, async (game) => {
            await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
            expect(await game.problems()).toEqual([]);
        }), TEST_TIMEOUT);
} else {
    test.skip("[needs SOH_WASM_BAKED_BUILD] a baked build boots from its own index.html", () => {});
}

// S1: SaveManager reads every save's metadata at startup without catching a parse error,
// and leaves its mutex locked when one throws. LoadFile already catches and keeps a .bak.
knownBug("S1", "a save that is not JSON does not stop the game at boot", () =>
    withGame(suite, { inlineFiles: { "/Save/file2.sav": "this is not a save" } }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        await Bun.sleep(2000);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);
