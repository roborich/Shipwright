// A module worker running the converter the way a host would: fetch the ROM, hand the
// bytes to extractRom, relay progress, post the result. See soh/wasm/HOST-API.md.
self.onmessage = async ({ data }) => {
    try {
        const { default: createSohExtractor } = await import(data.moduleUrl);
        const mod = await createSohExtractor({ print() {}, printErr() {} });
        const response = await fetch(data.romUrl);
        if (!response.ok) throw new Error(`${data.romUrl} returned HTTP ${response.status}`);
        const rom = new Uint8Array(await response.arrayBuffer());
        const started = performance.now();
        const result = await mod.extractRom(rom, {
            onProgress: (done, total) => self.postMessage({ type: 'progress', done, total }),
        });
        self.postMessage({
            type: 'done',
            name: result.name,
            version: result.version,
            byteLength: result.bytes.length,
            seconds: (performance.now() - started) / 1000,
        });
    } catch (error) {
        self.postMessage({ type: 'error', message: String((error && error.message) || error), code: error && error.code });
    }
};
