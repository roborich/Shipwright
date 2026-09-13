import { expect, test } from "bun:test";
import { playFiles } from "./lib/configs";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { loadFirstSave, type Game } from "./lib/game";
import { knownBug } from "./lib/known";
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

knownBug("C1", "a command sent from inside a scene listener takes effect", () =>
    withGame(suite, {}, async (game) => {
        await bootToScene(game);
        const from = await game.eventCount();
        await game.armCommandOnEvent("scene", "entrance 0");
        expect(await game.run("entrance cd")).toBe(0);
        await game.waitForEvent("scene", { from, match: (e) => e.entranceIndex === 0, timeout: 30_000 });
        expect(await game.listenerResults()).toEqual([0]);
    }), TEST_TIMEOUT);

knownBug("C2", "load-game arrives once play-state commands work", () =>
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

knownBug("C3", "a save that disappears is reported as file-removed", () =>
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
