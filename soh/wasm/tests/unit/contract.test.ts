import { expect, test } from "bun:test";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { documentedEventTypes, emittedEventTypes } from "../lib/contract";
import { REPO_DIR } from "../lib/env";

test("emittedEventTypes finds JSON and EM_ASM event types", () => {
    const source = `fmt::format(R"({{"type":"scene","sceneNum":{}}})"); EM_ASM({ detail : { type : 'file-saved' } });`;
    expect(emittedEventTypes(source)).toEqual(["file-saved", "scene"]);
});

test("documentedEventTypes reads only the events table", () => {
    const doc = "## 2. Events out\n| `quit` | — | x |\n| `error` | `message` | x |\n## 3. Commands in\n| `reset` | no | x |\n";
    expect(documentedEventTypes(doc)).toEqual(["error", "quit"]);
});

test("every event the bridge sends is in HOST-API.md, and nothing else is", () => {
    const bridge = readFileSync(join(REPO_DIR, "soh/soh/EmbedderBridge.cpp"), "utf8");
    const hostApi = readFileSync(join(REPO_DIR, "soh/wasm/HOST-API.md"), "utf8");
    expect(emittedEventTypes(bridge).length).toBeGreaterThan(0);
    expect(documentedEventTypes(hostApi)).toEqual(emittedEventTypes(bridge));
});
