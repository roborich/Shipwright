import { expect, test } from "bun:test";
import { contentType, findMount, resolveWithin } from "../lib/server";

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
