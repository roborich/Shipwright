import { expect, test } from "bun:test";
import { audioDrains, audioDropsDuring, drawsPerTick, tickRateSegments, type StatsSample } from "../lib/timing";

const sample = (t: number, ticks: number, updateRate: number, draws = ticks, audioBuffered = 0, audioDrops = 0): StatsSample => ({
    t,
    ticks,
    draws,
    updateRate,
    audioBuffered,
    audioDrops,
});

test("tickRateSegments measures each constant-rate run against 60 / R_UPDATE_RATE", () => {
    const samples = [sample(0, 0, 1), sample(1000, 60, 1), sample(2000, 120, 1), sample(2500, 125, 3), sample(4500, 165, 3)];
    const segments = tickRateSegments(samples, 1000);
    expect(segments.map((s) => [s.updateRate, s.expectedHz, s.measuredHz])).toEqual([
        [1, 60, 60],
        [3, 20, 20],
    ]);
    expect(segments[0].error).toBe(0);
});

test("tickRateSegments drops runs shorter than the minimum", () => {
    const samples = [sample(0, 0, 2), sample(500, 15, 2), sample(600, 15, 3), sample(3600, 75, 3)];
    expect(tickRateSegments(samples, 1000).map((s) => s.updateRate)).toEqual([3]);
});

test("tickRateSegments reports a loop running 4% fast", () => {
    const [segment] = tickRateSegments([sample(0, 0, 1), sample(10_000, 625, 1)], 1000);
    expect(segment.error).toBeCloseTo(0.0417, 3);
});

test("drawsPerTick and the audio helpers", () => {
    const samples = [sample(0, 10, 3, 10, 4000, 2), sample(1000, 30, 3, 70, 5000, 2), sample(2000, 50, 3, 130, 4600, 5)];
    expect(drawsPerTick(samples)).toBe(3);
    expect(audioDropsDuring(samples)).toBe(3);
    expect(audioDrains(samples)).toBe(true);
    expect(audioDrains(samples.slice(0, 2))).toBe(false);
});
