import { expect, test } from "bun:test";
import { baseConfig, playConfig } from "./lib/configs";
import { BOOT_TIMEOUT, TEST_TIMEOUT } from "./lib/env";
import { useSuite, withGame } from "./lib/suite";
import { audioDrains } from "./lib/timing";

const suite = useSuite();

const configWith = (backend: string) => {
    const config = baseConfig();
    config.Window = { ...config.Window, AudioBackend: backend };
    return { "/shipofharkinian.json": JSON.stringify(config) };
};

// The backend the game settled on, as it wrote it back to the config.
async function savedBackend(game: any): Promise<string | undefined> {
    const text = await game.savedText("/shipofharkinian.json");
    return text ? JSON.parse(text).Window?.AudioBackend : undefined;
}

// A config from a Mac install names "coreaudio". LUS used to fall through to the null player
// for any backend it was not built with, so the game ran silent with nothing in the console.
// It now takes the platform's first backend, which here is the Web Audio player.
test("a config naming a backend this build lacks plays through Web Audio", () =>
    withGame(suite, { inlineFiles: configWith("coreaudio") }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        const samples = await game.sampleStats(5000, 100);
        expect(audioDrains(samples)).toBe(true);
        expect(await savedBackend(game)).toBe("webaudio");
        const audio = await game.webAudio();
        expect(audio).not.toBeNull();
        expect(audio!.ready).toBe(true);
        expect(audio!.contextState).toBe("running");
    }), TEST_TIMEOUT);

// The worklet runs on the audio thread, so once the title screen is up and the queue is
// primed it should never have to fill a block with silence.
test("the Web Audio worklet consumes without underruns on the title screen", () =>
    withGame(suite, { inlineFiles: { "/shipofharkinian.json": JSON.stringify(playConfig()) } }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        await Bun.sleep(2000); // let the queue reach its target
        const before = (await game.webAudio())!;
        const samples = await game.sampleStats(5000, 100);
        const after = (await game.webAudio())!;
        expect(audioDrains(samples)).toBe(true);
        expect(after.consumed).toBeGreaterThan(before.consumed);
        expect(after.underruns - before.underruns).toBe(0);
        expect(samples[samples.length - 1].audioDrops - samples[0].audioDrops).toBe(0);
    }), TEST_TIMEOUT);

// SDL's ScriptProcessorNode player stays as the fallback for browsers without AudioWorklet,
// and a config may still name it.
test("the SDL player still plays when the config names it", () =>
    withGame(suite, { inlineFiles: configWith("sdl") }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        const samples = await game.sampleStats(5000, 100);
        expect(audioDrains(samples)).toBe(true);
        expect(await game.webAudio()).toBeNull();
    }), TEST_TIMEOUT);
