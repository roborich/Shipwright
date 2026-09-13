import { existsSync } from "node:fs";
import { join, resolve } from "node:path";

export const TESTS_DIR = resolve(import.meta.dir, "..");
export const REPO_DIR = resolve(TESTS_DIR, "../../..");

export const BUILD_DIR = resolve(process.env.SOH_WASM_BUILD ?? join(REPO_DIR, "build-wasm-rel/soh"));
export const FILES_DIR = resolve(process.env.SOH_WASM_FILES ?? join(BUILD_DIR, "hostfiles"));

export const SLOW = process.env.SOH_WASM_SLOW === "1";
export const SOAK = process.env.SOH_WASM_SOAK === "1";
export const HEADED = process.env.SOH_WASM_HEADED === "1";
export const RUN_KNOWN_BUGS = process.env.SOH_WASM_KNOWN_BUGS === "1";

// A release build boots in well under a minute; eager tier-up and a cold cache take longer.
export const BOOT_TIMEOUT = 120_000;
export const TEST_TIMEOUT = 300_000;

export function requireBuild(): void {
    const missing = [join(BUILD_DIR, "soh.js"), join(BUILD_DIR, "soh.wasm"), join(FILES_DIR, "oot.o2r")].filter(
        (path) => !existsSync(path),
    );
    if (missing.length) {
        throw new Error(
            `Missing ${missing.join(", ")}. Build the wasm target and point SOH_WASM_BUILD / ` +
                `SOH_WASM_FILES at it (see soh/wasm/tests/README.md).`,
        );
    }
}
