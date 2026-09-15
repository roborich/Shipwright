// Just enough of the zip format to list an .o2r: names, sizes and CRCs from the central
// directory. Enough to say whether two archives hold the same entries with the same bytes,
// without inflating anything (the archives' own timestamps differ between runs).

export type ZipEntry = { name: string; size: number; crc: number };

const EOCD_SIGNATURE = 0x06054b50;
const CENTRAL_SIGNATURE = 0x02014b50;
const EOCD_MIN = 22;
const COMMENT_MAX = 0xffff;

function findEndOfCentralDirectory(view: DataView): number {
    const start = Math.max(0, view.byteLength - EOCD_MIN - COMMENT_MAX);
    for (let offset = view.byteLength - EOCD_MIN; offset >= start; offset--) {
        if (view.getUint32(offset, true) === EOCD_SIGNATURE) {
            return offset;
        }
    }
    throw new Error("zip: no end-of-central-directory record");
}

export function zipEntries(bytes: Uint8Array): ZipEntry[] {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const eocd = findEndOfCentralDirectory(view);
    const count = view.getUint16(eocd + 10, true);
    let offset = view.getUint32(eocd + 16, true);
    if (count === 0xffff || offset === 0xffffffff) {
        throw new Error("zip: zip64 archive; this reader handles only classic central directories");
    }
    const decoder = new TextDecoder();
    const entries: ZipEntry[] = [];
    for (let i = 0; i < count; i++) {
        if (view.getUint32(offset, true) !== CENTRAL_SIGNATURE) {
            throw new Error(`zip: bad central directory header at ${offset}`);
        }
        const crc = view.getUint32(offset + 16, true);
        const size = view.getUint32(offset + 24, true);
        const nameLength = view.getUint16(offset + 28, true);
        const extraLength = view.getUint16(offset + 30, true);
        const commentLength = view.getUint16(offset + 32, true);
        const name = decoder.decode(bytes.subarray(offset + 46, offset + 46 + nameLength));
        entries.push({ name, size, crc });
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
}

export function zipIndex(bytes: Uint8Array): Map<string, ZipEntry> {
    return new Map(zipEntries(bytes).map((entry) => [entry.name, entry]));
}

// Entries present in one archive but not the other, or with different bytes.
export function zipDifferences(a: Map<string, ZipEntry>, b: Map<string, ZipEntry>): string[] {
    const differences: string[] = [];
    for (const [name, entry] of a) {
        const other = b.get(name);
        if (!other) {
            differences.push(`only in first: ${name}`);
        } else if (other.crc !== entry.crc || other.size !== entry.size) {
            differences.push(`different: ${name}`);
        }
    }
    for (const name of b.keys()) {
        if (!a.has(name)) {
            differences.push(`only in second: ${name}`);
        }
    }
    return differences;
}
