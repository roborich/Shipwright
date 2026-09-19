// Pure analysis of Soh_GetStats samples taken over time.

export type StatsSample = {
    t: number; // ms, from performance.now()
    ticks: number;
    draws: number;
    updateRate: number;
    audioBuffered: number;
    audioDrops: number;
};

export type RateSegment = { updateRate: number; durationMs: number; measuredHz: number; expectedHz: number; error: number };

// Splits samples into runs where R_UPDATE_RATE held still, and measures the tick rate of each
// run that lasted at least minDurationMs. error is measured / expected - 1.
export function tickRateSegments(samples: StatsSample[], minDurationMs: number): RateSegment[] {
    const runs: StatsSample[][] = [];
    for (const sample of samples) {
        const run = runs[runs.length - 1];
        if (run && run[0].updateRate === sample.updateRate) {
            run.push(sample);
        } else {
            runs.push([sample]);
        }
    }
    return runs
        .filter((run) => run.length > 1 && run[0].updateRate > 0)
        .map((run) => {
            const first = run[0];
            const last = run[run.length - 1];
            const durationMs = last.t - first.t;
            const measuredHz = ((last.ticks - first.ticks) * 1000) / durationMs;
            const expectedHz = 60 / first.updateRate;
            return { updateRate: first.updateRate, durationMs, measuredHz, expectedHz, error: measuredHz / expectedHz - 1 };
        })
        .filter((segment) => segment.durationMs >= minDurationMs);
}

export function drawsPerTick(samples: StatsSample[]): number {
    const first = samples[0];
    const last = samples[samples.length - 1];
    return (last.draws - first.draws) / (last.ticks - first.ticks);
}

// Audio updates the player discarded, because its queue was already full, between the first
// sample and the last.
export function audioDropsDuring(samples: StatsSample[]): number {
    return samples[samples.length - 1].audioDrops - samples[0].audioDrops;
}

// True when the audio queue went down at some point, i.e. the device is consuming it. A
// suspended AudioContext never drains, which would make every buffer assertion meaningless.
export function audioDrains(samples: StatsSample[]): boolean {
    return samples.some((s, i) => i > 0 && s.audioBuffered < samples[i - 1].audioBuffered);
}
