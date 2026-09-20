import { expect, test } from "bun:test";
import { playConfig, playFiles } from "./lib/configs";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { loadFirstSave, type Game } from "./lib/game";
import { useSuite, withGame } from "./lib/suite";

// soh/wasm/HOST-API.md is the spec for everything in this file.
const suite = useSuite();

const bootToScene = (game: Game) => game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });

test("an unknown command returns -2; entrance warps the title screen", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        expect(await game.run("no_such_command")).toBe(-2);
        const from = await game.eventCount();
        expect(await game.run("entrance 0")).toBe(0);
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0, timeout: 30_000 });
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

// Entrance 0 is the Deku Tree, whose four layers all load the same scene; bb is Link's House,
// whose floor is at y = 0 around the origin, so a point there has a floor under it.
test("warp starts a fresh game from the title screen, as the age and time asked for", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        const from = await game.eventCount();
        expect(await game.run("warp 0 child night")).toBe(0);
        const loaded = await game.waitForEvent("load-game", { from, timeout: 30_000 });
        expect(loaded.event.fileNum).toBe(255);
        const { event } = await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0, timeout: 30_000 });
        expect(event.setup).toBe(1);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

// With the Freeze Time cheat on, the cheat writes its remembered time over the clock every
// frame: the night layers below land only because a warp retimes it.
const freezeTimeConfig = () => {
    const config = playConfig();
    config.CVars.gCheats = { ...config.CVars.gCheats, FreezeTime: 1 };
    return config;
};

test("warp in gameplay switches age and time in place, and can stand at a point", () =>
    withGame(suite, { inlineFiles: playFiles(freezeTimeConfig()) }, async (game) => {
        await bootToScene(game);
        let from = await game.eventCount();
        expect(await game.run("warp 0 adult day")).toBe(0); // from the attract demo: a fresh game
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0 && e.setup === 2, timeout: 30_000 });

        from = await game.eventCount();
        expect(await game.run("warp 0 child c001")).toBe(0);
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0 && e.setup === 1, timeout: 30_000 });

        from = await game.eventCount();
        expect(await game.run("warp bb adult 8000 0 0 0 0 0")).toBe(0);
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0xbb && e.setup === 2, timeout: 30_000 });

        from = await game.eventCount();
        expect(await game.run("warp 0")).toBe(0); // the defaults: adult, noon
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0 && e.setup === 2, timeout: 30_000 });
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("warp refuses what it cannot parse", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        expect(await game.run("warp")).toBe(1);
        expect(await game.run("warp zz")).toBe(1);
        expect(await game.run("warp -1")).toBe(1);
        expect(await game.run("warp 614")).toBe(1); // ENTR_MAX
        expect(await game.run("warp 0 child day 40000 0 0 0 0")).toBe(1); // room past s16
        expect(await game.run("warp 0 teen")).toBe(1); // a word that is not an age, and not a time either
        expect(await game.run("warp 0 child 10000")).toBe(1); // time past FFFF
        expect(await game.run("warp 0 child day 0 1 2")).toBe(1); // a point needs five numbers
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

const bootWarpConfig = (point: Record<string, unknown>) => {
    const config = playConfig();
    config.CVars.gSettings = { ...config.CVars.gSettings, BootSequence: 4 };
    config.CVars.gDeveloperTools = { ...config.CVars.gDeveloperTools, DebugEnabled: 1 };
    config.WarpPoints = { test: { bootToPoint: true, entranceId: 0xbb, roomNum: 0, pos: { x: 0, y: 0, z: 0 }, rotY: 0, ...point } };
    return config;
};

test("a boot warp point loads at its own age and time, skipping the title screen", () =>
    withGame(suite, { inlineFiles: playFiles(bootWarpConfig({ linkAge: 1, dayTime: 0 })) }, async (game) => {
        const { event } = await bootToScene(game);
        expect(event.entranceIndex).toBe(0xbb);
        expect(event.setup).toBe(1);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("a boot warp point can name its entrance, and the name wins over the index", () =>
    withGame(suite, { inlineFiles: playFiles(bootWarpConfig({ entranceName: "ENTR_DEKU_TREE_ENTRANCE", linkAge: 1 })) }, async (game) => {
        const { event } = await bootToScene(game);
        expect(event.entranceIndex).toBe(0);
        expect(event.setup).toBe(0);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("a boot warp point saved without an age or time still means adult at noon", () =>
    withGame(suite, { inlineFiles: playFiles(bootWarpConfig({})) }, async (game) => {
        const { event } = await bootToScene(game);
        expect(event.entranceIndex).toBe(0xbb);
        expect(event.setup).toBe(2);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("play-state commands return 1 on file select", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        expect(await game.run("file_select")).toBe(0);
        await Bun.sleep(3000);
        expect(await game.run("entrance cd")).toBe(1);
        expect(await game.run("reload")).toBe(1);
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("a save loads from file select, and a scene follows load-game", () =>
    withGame(suite, { inlineFiles: playFiles() }, async (game) => {
        await bootToScene(game);
        const loaded = await loadFirstSave(game);
        expect(loaded.event.fileNum).toBe(0);
        await game.waitForEvent("scene", { from: loaded.index, timeout: 30_000 });
        expect(await game.problems()).toEqual([]);
    }), TEST_TIMEOUT);

test("a command sent from inside a scene listener takes effect", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        const from = await game.eventCount();
        await game.armCommandOnEvent("scene", "entrance 0");
        expect(await game.run("entrance cd")).toBe(0);
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0, timeout: 30_000 });
        expect(await game.listenerResults()).toEqual([0]);
    }), TEST_TIMEOUT);

test("load-game arrives once play-state commands work", () =>
    withGame(suite, { inlineFiles: playFiles() }, async (game) => {
        await bootToScene(game);
        await game.armCommandOnEvent("load-game", "entrance cd");
        const loaded = await loadFirstSave(game);
        expect(await game.listenerResults()).toEqual([0]);
        await game.waitForEvent("scene", { from: loaded.index, match: (e) => e.entranceIndex === 0xcd, timeout: 30_000 });
    }), TEST_TIMEOUT);

test("file-saved reports files written after boot, not the files supplied", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        await Bun.sleep(2000);
        const echoed = (await game.events()).filter((e) => e.type === "file-saved" && e.path.startsWith("/Save/"));
        expect(echoed).toEqual([]);
        await game.writeFile("/Save/file3.sav", "hello");
        const { event } = await game.waitForEvent("file-saved", { match: (e) => e.path === "/Save/file3.sav" });
        expect(event.byteLength).toBe(5);
    }), TEST_TIMEOUT);

test("a save that disappears is reported as file-removed", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        await game.writeFile("/Save/file3.sav", "hello");
        const saved = await game.waitForEvent("file-saved", { match: (e) => e.path === "/Save/file3.sav" });
        await game.unlink("/Save/file3.sav");
        await game.waitForEvent("file-removed", { from: saved.index, match: (e) => e.path === "/Save/file3.sav", timeout: 10_000 });
    }), TEST_TIMEOUT);

test("quit sends the last file-saved events before quit", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        await game.writeFile("/Save/file2.sav", "last words");
        expect(await game.run("quit")).toBe(0);
        const quit = await game.waitForEvent("quit", { timeout: 30_000 });
        const saved = await game.waitForEvent("file-saved", { match: (e) => e.path === "/Save/file2.sav", timeout: 1 });
        expect(saved.index).toBeLessThan(quit.index);
    }), TEST_TIMEOUT);
