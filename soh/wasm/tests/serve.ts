// Serves a wasm build for playing it in a browser: `bun run serve`.
//
// The build directory is served at /, so host.html works as it does when deployed, and the
// game files (SOH_WASM_FILES) at /hostfiles/, which is where host.html fetches them from. PORT picks the port (default 8724); HOST=0.0.0.0 serves the network.
import { BUILD_DIR, FILES_DIR, requireBuild } from "./lib/env";
import { startServer } from "./lib/server";

requireBuild();

const port = Number(process.env.PORT ?? 8724);
const hostname = process.env.HOST ?? "127.0.0.1";
const server = startServer({ "/": BUILD_DIR, "/hostfiles/": FILES_DIR }, { port, hostname, redirects: { "/": "/host.html" } });

console.log(`Serving ${BUILD_DIR}`);
console.log(`  game files from ${FILES_DIR}`);
console.log("");
console.log(`  ${server.url}/host.html            play`);
console.log(`  ${server.url}/host.html?memtrace   play, with the memory trace`);
console.log("");
console.log("Rebuild with `make -C build-wasm-rel soh -j10` and reload; nothing is cached. Ctrl+C stops.");
