import { expect, test } from "bun:test";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { DEFAULT_FILES, withoutFile } from "./lib/game";
import { useSuite, withGame, withPage } from "./lib/suite";

const suite = useSuite();

test("a missing oot.o2r is reported as an error event", () =>
    withGame(suite, { files: withoutFile(DEFAULT_FILES, "/oot.o2r") }, async (game) => {
        const { event } = await game.waitForEvent("error", { timeout: BOOT_TIMEOUT });
        expect(event.message).toContain("oot.o2r");
    }), TEST_TIMEOUT);

test("host.html boots with every host file present", () =>
    withPage(suite, "/build/host.html", undefined, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("host.html boots cleanly when an optional save is missing", () =>
    withPage(
        suite,
        "/build/host.html",
        (page) => page.route("**/hostfiles/Save/file1.sav", (route) => route.fulfill({ status: 404, body: "not found" })),
        async (game) => {
            await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
            expect((await game.listDir("/Save")).filter((name) => name.endsWith(".bak"))).toEqual([]);
        },
    ), TEST_TIMEOUT);

// SaveManager read every save's metadata at startup without catching a parse error, and left
// its mutex locked when one threw. It now moves the file aside, as LoadFile already did.
test("a save that is not JSON does not stop the game at boot", () =>
    withGame(suite, { inlineFiles: { "/Save/file2.sav": "this is not a save" } }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        await Bun.sleep(2000);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);
