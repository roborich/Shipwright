// Runs `bun test` with a wall-clock budget. bun's per-test timeout does not cover hooks or
// the runner's own exit, and a wedged renderer once kept a run alive for hours (see
// README.md, "If a run hangs"). Past the budget the run gets SIGTERM, which lets bun's exit
// handlers close Playwright's browser, then SIGKILL. Arguments pass through to bun test.
//
//   bun run-tests.ts smoke            # same as `bun test smoke`, capped
//   SOH_WASM_BUDGET_MIN=45 bun run-tests.ts
const budgetMinutes = Number(process.env.SOH_WASM_BUDGET_MIN ?? 30);
const args = process.argv.slice(2);

// bun 1.2.x closes Playwright's DevTools pipe partway through a multi-file run (Chromium logs
// "Connection terminated while reading from pipe" and exits cleanly), after which every
// remaining test waits out its full timeout on a browser that no longer exists. 1.4.2 runs the
// suite clean. Refuse to start on a version known to do this rather than fail 20 minutes in.
const MIN_BUN = [1, 4];
const [major, minor] = Bun.version.split(".").map(Number);
if (major < MIN_BUN[0] || (major === MIN_BUN[0] && minor < MIN_BUN[1])) {
    console.error(`run-tests: bun ${Bun.version} drops the browser connection mid-run; needs ${MIN_BUN.join(".")}+ (bun upgrade)`);
    process.exit(2);
}

const child = Bun.spawn(["bun", "test", ...args], { stdio: ["inherit", "inherit", "inherit"] });
const watchdog = setTimeout(() => {
    console.error(`\nrun-tests: no result after ${budgetMinutes} min; stopping bun test (pid ${child.pid})`);
    child.kill("SIGTERM");
    setTimeout(() => {
        child.kill("SIGKILL");
        process.exit(124);
    }, 5000);
}, budgetMinutes * 60_000);

const code = await child.exited;
clearTimeout(watchdog);
process.exit(code);
