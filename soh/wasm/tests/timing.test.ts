import { expect } from "bun:test";
import { playConfig, playFiles } from "./lib/configs";
import { BOOT_TIMEOUT, SLOW, TEST_TIMEOUT } from "./lib/env";
import { loadFirstSave, type Game } from "./lib/game";
import { knownBug } from "./lib/known";
import { useSuite, withGame } from "./lib/suite";
import { audioDrains, drawsPerTick, maxAudioBuffered, tickRateSegments } from "./lib/timing";

// Opt-in (SOH_WASM_SLOW=1): these sample the running game for tens of seconds, and their
// numbers mean little on a machine that is busy with something else.
const suite = useSuite();

// LUS's SDL player drops a whole packet when this many samples are already queued.
const SDL_DROP_THRESHOLD = 6000;

async function bootAndLoad(game: Game): Promise<void> {
    await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
    const loaded = await loadFirstSave(game);
    await game.waitForEvent("scene", { from: loaded.index, timeout: 30_000 });
    await Bun.sleep(3000); // let the scene settle
}

knownBug("A2", "the loop ticks at 60 / R_UPDATE_RATE Hz, within 2%", () =>
    withGame(suite, { inlineFiles: playFiles() }, async (game) => {
        await game.waitForEvent("scene", { timeout: BOOT_TIMEOUT });
        const title = await game.sampleStats(12_000);
        await bootAndLoad(game);
        const gameplay = await game.sampleStats(12_000);
        const segments = [...tickRateSegments(title, 5000), ...tickRateSegments(gameplay, 5000)];
        expect(segments.length).toBeGreaterThan(0);
        expect(segments.filter((s) => Math.abs(s.error) > 0.02)).toEqual([]);
    }), TEST_TIMEOUT, SLOW);

knownBug("A1", "the audio queue never reaches the SDL player's drop threshold", () =>
    withGame(suite, { inlineFiles: playFiles() }, async (game) => {
        await bootAndLoad(game);
        const samples = await game.sampleStats(30_000, 100);
        expect(audioDrains(samples)).toBe(true); // otherwise the device is not playing and this proves nothing
        expect(maxAudioBuffered(samples)).toBeLessThan(SDL_DROP_THRESHOLD);
    }), TEST_TIMEOUT, SLOW);

knownBug("A3", "60 fps interpolation still draws one frame per tick", () => {
    const config = playConfig();
    config.CVars.gSettings = { ...config.CVars.gSettings, InterpolationFPS: 60 };
    return withGame(suite, { inlineFiles: playFiles(config) }, async (game) => {
        await bootAndLoad(game);
        const samples = await game.sampleStats(5000);
        expect(samples[0].updateRate).toBe(3);
        expect(drawsPerTick(samples)).toBeCloseTo(1, 1);
    });
}, TEST_TIMEOUT, SLOW);
