import { expect, test } from "bun:test";
import { exportNames, functionBodies, largestFunctions, readLeb128 } from "../lib/wasm";

// One imported function ("m"."f"), two defined functions named "a" (2-byte body) and "b"
// (4-byte body), "b" exported.
const MODULE = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // magic, version
    0x01, 0x04, 0x01, 0x60, 0x00, 0x00, // type: () -> ()
    0x02, 0x07, 0x01, 0x01, 0x6d, 0x01, 0x66, 0x00, 0x00, // import m.f
    0x03, 0x03, 0x02, 0x00, 0x00, // two functions
    0x07, 0x05, 0x01, 0x01, 0x62, 0x00, 0x02, // export "b" = func 2
    0x0a, 0x09, 0x02, 0x02, 0x00, 0x0b, 0x04, 0x00, 0x01, 0x01, 0x0b, // code
    0x00, 0x0e, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x01, 0x07, 0x02, 0x01, 0x01, 0x61, 0x02, 0x01, 0x62, // names
]);

test("the fixture is a valid module", () => {
    expect(() => new WebAssembly.Module(MODULE)).not.toThrow();
});

test("readLeb128 decodes multi-byte values", () => {
    expect(readLeb128(new Uint8Array([0xe5, 0x8e, 0x26]), 0)).toEqual({ value: 624485, next: 3 });
    expect(readLeb128(new Uint8Array([0x80, 0x80, 0x80, 0x80, 0x10]), 0)).toEqual({ value: 2 ** 32, next: 5 });
});

test("functionBodies indexes past imports and applies the name section", () => {
    expect(functionBodies(MODULE)).toEqual([
        { index: 1, name: "a", size: 2 },
        { index: 2, name: "b", size: 4 },
    ]);
    expect(largestFunctions(MODULE, 1)).toEqual([{ index: 2, name: "b", size: 4 }]);
});

test("exportNames lists the export section", () => {
    expect(exportNames(MODULE)).toEqual(["b"]);
});
