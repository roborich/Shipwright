// Reads the event contract out of the bridge source and out of HOST-API.md, so a test can
// hold them to each other.

function unique(values: Iterable<string>): string[] {
    return [...new Set(values)].sort();
}

// Event types the bridge sends: `"type":"x"` in its JSON strings and `type : 'x'` in EM_ASM.
export function emittedEventTypes(bridgeSource: string): string[] {
    const json = [...bridgeSource.matchAll(/"type":"([a-z-]+)"/g)].map((m) => m[1]);
    const js = [...bridgeSource.matchAll(/type\s*:\s*'([a-z-]+)'/g)].map((m) => m[1]);
    return unique([...json, ...js]);
}

// Event types in the table under HOST-API.md's "Events out" heading.
export function documentedEventTypes(hostApi: string): string[] {
    const start = hostApi.search(/^## .*Events out/m);
    if (start < 0) {
        return [];
    }
    const rest = hostApi.slice(start + 1);
    const end = rest.search(/^## /m);
    const section = end < 0 ? rest : rest.slice(0, end);
    return unique([...section.matchAll(/^\|\s*`([a-z-]+)`\s*\|/gm)].map((m) => m[1]));
}
