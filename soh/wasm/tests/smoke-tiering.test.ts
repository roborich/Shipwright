import { expect, test } from "bun:test";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { useSuite, withGame } from "./lib/suite";

// Compile every function with V8's optimising compiler up front instead of when it gets hot.
// A 3.3 MB function (Binaryen had inlined all of RegionTable_Init's builders into it) killed
// the renderer this way in seconds, where normal play took minutes to reach it.
const suite = useSuite(["--no-wasm-dynamic-tiering", "--no-wasm-lazy-compilation"]);

test("survives eager tier-up of every function", () =>
    withGame(suite, {}, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT * 2 });
        await Bun.sleep(10_000);
        expect(game.crashed).toBe(false);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);
