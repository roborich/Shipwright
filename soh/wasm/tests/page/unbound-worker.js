// A module worker preparing SoH: Unbound the way a host would: from a ROM (soh-extract, then the
// Unbound converter) or from an oot.o2r it already has (the converter alone). Posts the base's size,
// SHA-256 and report; the bytes stay here. See soh/wasm/HOST-API.md §6 and §7.
async function fetchBytes(url) {
    const response = await fetch(url);
    if (!response.ok) throw new Error(`${url} returned HTTP ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}

async function romToO2r(romUrl, extractorUrl) {
    const { default: createSohExtractor } = await import(extractorUrl);
    const extractor = await createSohExtractor({ print() {}, printErr() {} });
    const { bytes } = await extractor.extractRom(await fetchBytes(romUrl));
    return bytes; // the extractor instance, and its heap, go out of scope here
}

self.onmessage = async ({ data }) => {
    try {
        const started = performance.now();
        const oot = data.romUrl ? await romToO2r(data.romUrl, data.extractorUrl) : await fetchBytes(data.o2rUrl);
        const { default: createSohUnboundConverter } = await import(data.converterUrl);
        const converter = await createSohUnboundConverter({ print() {}, printErr() {} });
        const { bytes, report } = await converter.convertToUnbound(oot);
        const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
        self.postMessage({
            type: 'done',
            report,
            byteLength: bytes.length,
            sha256: Array.from(digest, (b) => b.toString(16).padStart(2, '0')).join(''),
            seconds: (performance.now() - started) / 1000,
        });
    } catch (error) {
        self.postMessage({ type: 'error', message: String((error && error.message) || error), code: error && error.code });
    }
};
