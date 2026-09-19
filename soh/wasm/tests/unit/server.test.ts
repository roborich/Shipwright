import { expect, test } from "bun:test";
import { contentType, findMount, resolveWithin, startServer } from "../lib/server";

test("resolveWithin maps request paths inside the root", () => {
    expect(resolveWithin("/srv/build", "soh.js")).toBe("/srv/build/soh.js");
    expect(resolveWithin("/srv/build", "/hostfiles/Save/file1.sav")).toBe("/srv/build/hostfiles/Save/file1.sav");
});

test("resolveWithin refuses paths that escape the root or do not decode", () => {
    expect(resolveWithin("/srv/build", "../secret")).toBeNull();
    expect(resolveWithin("/srv/build", "%2e%2e/secret")).toBeNull();
    expect(resolveWithin("/srv/build", "")).toBeNull();
    expect(resolveWithin("/srv/build", "%E0%A4%A")).toBeNull();
});

test("findMount picks the longest matching prefix", () => {
    const mounts = { "/build/": "/a", "/build/hostfiles/": "/b" };
    expect(findMount(mounts, "/build/soh.js")).toEqual({ dir: "/a", rest: "soh.js" });
    expect(findMount(mounts, "/build/hostfiles/oot.o2r")).toEqual({ dir: "/b", rest: "oot.o2r" });
    expect(findMount(mounts, "/elsewhere")).toBeNull();
});

test("contentType serves wasm as application/wasm, for streaming compilation", () => {
    expect(contentType("soh.wasm")).toBe("application/wasm");
    expect(contentType("soh.data")).toBe("application/octet-stream");
});

test("startServer serves mounts, redirects, and refuses what is outside them", async () => {
    const { mkdtempSync, mkdirSync, writeFileSync } = await import("node:fs");
    const { join } = await import("node:path");
    const { tmpdir } = await import("node:os");
    const root = mkdtempSync(join(tmpdir(), "soh-serve-"));
    mkdirSync(join(root, "build"));
    mkdirSync(join(root, "files", "Save"), { recursive: true });
    writeFileSync(join(root, "build", "soh.wasm"), "wasm");
    writeFileSync(join(root, "files", "Save", "file1.sav"), "save");
    writeFileSync(join(root, "secret"), "no");

    const server = startServer(
        { "/": join(root, "build"), "/hostfiles/": join(root, "files") },
        { redirects: { "/": "/host.html" } },
    );
    try {
        const home = await fetch(`${server.url}/`, { redirect: "manual" });
        expect([home.status, home.headers.get("location")]).toEqual([302, "/host.html"]);

        const wasm = await fetch(`${server.url}/soh.wasm`);
        expect([wasm.status, wasm.headers.get("content-type"), await wasm.text()]).toEqual([200, "application/wasm", "wasm"]);

        const save = await fetch(`${server.url}/hostfiles/Save/file1.sav`);
        expect([save.status, await save.text()]).toEqual([200, "save"]);

        expect((await fetch(`${server.url}/missing.js`)).status).toBe(404);
        expect((await fetch(`${server.url}/%2e%2e/secret`)).status).toBe(404);
    } finally {
        server.stop();
    }
});
