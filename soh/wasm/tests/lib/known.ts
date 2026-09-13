import { test } from "bun:test";
import { RUN_KNOWN_BUGS } from "./env";

// A test for a bug that is known and not fixed yet. Skipped unless SOH_WASM_KNOWN_BUGS=1,
// which runs it to confirm it still fails. The commit that fixes the bug makes it a `test`.
// `enabled: false` skips it regardless, for tests that also need an opt-in such as SOH_WASM_SLOW.
export function knownBug(id: string, name: string, fn: () => Promise<void>, timeout: number, enabled = true): void {
    (RUN_KNOWN_BUGS && enabled ? test : test.skip)(`[known bug ${id}] ${name}`, fn, timeout);
}
