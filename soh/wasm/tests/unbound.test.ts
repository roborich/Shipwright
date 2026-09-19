import { expect, test } from "bun:test";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { BOOT_TIMEOUT, FILES_DIR, TEST_TIMEOUT } from "./lib/env";
import { DEFAULT_FILES, withoutFile } from "./lib/game";
import { useSuite, withGame } from "./lib/suite";

// The game archive of SoH: Unbound, HOST-API.md §1: oot-unbound.o2r alone, or oot.o2r for the
// game to convert and hand back.
const suite = useSuite();

const BASE = "/oot-unbound.o2r";
const withBase = (rel: string) => ({ ...withoutFile(DEFAULT_FILES, "/oot.o2r"), [BASE]: rel });

// A converted base from any build of the same release: the one a desktop build writes next to
// its oot.o2r, or the bytes of this file's first test.
const HAVE_BASE = existsSync(join(FILES_DIR, "oot-unbound.o2r"));

test("oot.o2r alone is converted at startup and handed back as oot-unbound.o2r", () =>
    withGame(suite, {}, async (game) => {
        const saved = await game.waitForEvent("file-saved", { match: (e) => e.path === BASE, timeout: BOOT_TIMEOUT });
        expect(saved.event.byteLength).toBeGreaterThan(10_000_000);
        const scene = await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        expect(saved.index).toBeLessThan(scene.index);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test.skipIf(!HAVE_BASE)("oot-unbound.o2r alone boots without converting", () =>
    withGame(suite, { files: withBase("oot-unbound.o2r") }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        expect((await game.events()).filter((e) => e.path === BASE)).toEqual([]);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

// oot.o2r under the base's name: a complete game archive, but no Unbound manifest.
test("a standalone base that is not an Unbound conversion is reported as an error event", () =>
    withGame(suite, { files: withBase("oot.o2r") }, async (game) => {
        const { event } = await game.waitForEvent("error", { timeout: BOOT_TIMEOUT });
        expect(event.message).toContain("oot-unbound.o2r");
        expect(event.message).toContain("convert the ROM again");
    }), TEST_TIMEOUT);

// Custom entrances are numbered when the mods load, so a host names them ("<scene id>/<entrance id>",
// SPEC.md §7); vanilla entrances go by their enum names.
test("warp takes a registered entrance name", () =>
    withGame(suite, {}, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        expect(await game.run("warp NO_SUCH_ENTRANCE")).toBe(1);
        const from = await game.eventCount();
        expect(await game.run("warp ENTR_LINKS_HOUSE_CHILD_SPAWN child")).toBe(0);
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0xbb, timeout: 30_000 });
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);
