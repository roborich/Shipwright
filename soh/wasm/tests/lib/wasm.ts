// Just enough of the wasm binary format to measure function bodies and name them.

export type FunctionBody = { index: number; name: string; size: number; locals: number };

export function readLeb128(bytes: Uint8Array, offset: number): { value: number; next: number } {
    let value = 0;
    let scale = 1;
    let next = offset;
    for (;;) {
        const byte = bytes[next++];
        value += (byte & 0x7f) * scale;
        if ((byte & 0x80) === 0) {
            return { value, next };
        }
        scale *= 128;
    }
}

type Section = { id: number; start: number; end: number };

export function sections(bytes: Uint8Array): Section[] {
    const found: Section[] = [];
    let offset = 8; // magic + version
    while (offset < bytes.length) {
        const id = bytes[offset];
        const size = readLeb128(bytes, offset + 1);
        found.push({ id, start: size.next, end: size.next + size.value });
        offset = size.next + size.value;
    }
    return found;
}

function readName(bytes: Uint8Array, offset: number): { name: string; next: number } {
    const length = readLeb128(bytes, offset);
    const name = new TextDecoder().decode(bytes.subarray(length.next, length.next + length.value));
    return { name, next: length.next + length.value };
}

function skipLimits(bytes: Uint8Array, offset: number): number {
    const flags = bytes[offset];
    let next = readLeb128(bytes, offset + 1).next;
    if (flags & 1) {
        next = readLeb128(bytes, next).next;
    }
    return next;
}

// Imported functions come first in the function index space.
export function importedFunctionCount(bytes: Uint8Array, section: Section): number {
    let offset = section.start;
    const count = readLeb128(bytes, offset);
    offset = count.next;
    let functions = 0;
    for (let i = 0; i < count.value; i++) {
        offset = readName(bytes, offset).next; // module
        offset = readName(bytes, offset).next; // field
        const kind = bytes[offset++];
        if (kind === 0) {
            functions++;
            offset = readLeb128(bytes, offset).next;
        } else if (kind === 1) {
            offset = skipLimits(bytes, offset + 1);
        } else if (kind === 2) {
            offset = skipLimits(bytes, offset);
        } else if (kind === 3) {
            offset += 2;
        } else if (kind === 4) {
            offset = readLeb128(bytes, offset + 1).next;
        } else {
            throw new Error(`unknown import kind ${kind}`);
        }
    }
    return functions;
}

export function functionNames(bytes: Uint8Array, section: Section): Map<number, string> {
    const names = new Map<number, string>();
    let offset = readName(bytes, section.start).next; // past the custom section's own name
    while (offset < section.end) {
        const id = bytes[offset];
        const size = readLeb128(bytes, offset + 1);
        if (id === 1) {
            let cursor = size.next;
            const count = readLeb128(bytes, cursor);
            cursor = count.next;
            for (let i = 0; i < count.value; i++) {
                const index = readLeb128(bytes, cursor);
                const name = readName(bytes, index.next);
                names.set(index.value, name.name);
                cursor = name.next;
            }
        }
        offset = size.next + size.value;
    }
    return names;
}

export function functionBodies(bytes: Uint8Array): FunctionBody[] {
    const all = sections(bytes);
    const imports = all.find((s) => s.id === 2);
    const code = all.find((s) => s.id === 10);
    const nameSection = all.find((s) => s.id === 0 && readName(bytes, s.start).name === "name");
    if (!code) {
        return [];
    }
    const firstIndex = imports ? importedFunctionCount(bytes, imports) : 0;
    const names = nameSection ? functionNames(bytes, nameSection) : new Map<number, string>();
    const bodies: FunctionBody[] = [];
    const count = readLeb128(bytes, code.start);
    let offset = count.next;
    for (let i = 0; i < count.value; i++) {
        const size = readLeb128(bytes, offset);
        const index = firstIndex + i;
        bodies.push({ index, name: names.get(index) ?? `$func${index}`, size: size.value, locals: countLocals(bytes, size.next) });
        offset = size.next + size.value;
    }
    return bodies;
}

// Locals declared at the start of a function body: a vector of (count, type) groups.
export function countLocals(bytes: Uint8Array, bodyStart: number): number {
    const groups = readLeb128(bytes, bodyStart);
    let cursor = groups.next;
    let locals = 0;
    for (let g = 0; g < groups.value; g++) {
        const count = readLeb128(bytes, cursor);
        locals += count.value;
        const type = bytes[count.next];
        // (ref null <heaptype>) and (ref <heaptype>) carry a heap type index after the type byte.
        cursor = type === 0x63 || type === 0x64 ? readLeb128(bytes, count.next + 1).next : count.next + 1;
    }
    return locals;
}

export function mostLocals(bytes: Uint8Array, count: number): FunctionBody[] {
    return functionBodies(bytes)
        .sort((a, b) => b.locals - a.locals)
        .slice(0, count);
}

export function largestFunctions(bytes: Uint8Array, count: number): FunctionBody[] {
    return functionBodies(bytes)
        .sort((a, b) => b.size - a.size)
        .slice(0, count);
}

export function exportNames(bytes: Uint8Array): string[] {
    const section = sections(bytes).find((s) => s.id === 7);
    if (!section) {
        return [];
    }
    const names: string[] = [];
    const count = readLeb128(bytes, section.start);
    let offset = count.next;
    for (let i = 0; i < count.value; i++) {
        const name = readName(bytes, offset);
        names.push(name.name);
        offset = readLeb128(bytes, name.next + 1).next; // kind byte, then index
    }
    return names;
}
