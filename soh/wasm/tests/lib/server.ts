import { extname, resolve, sep } from "node:path";

// URL prefix (with trailing slash) -> directory served under it.
export type Mounts = Record<string, string>;
export type TestServer = { url: string; stop: () => void };

const CONTENT_TYPES: Record<string, string> = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".json": "application/json",
    ".md": "text/markdown; charset=utf-8",
    ".wasm": "application/wasm",
};

export function contentType(path: string): string {
    return CONTENT_TYPES[extname(path)] ?? "application/octet-stream";
}

// Maps a request path onto a file inside root, or null if it would escape root.
export function resolveWithin(root: string, requestPath: string): string | null {
    let decoded: string;
    try {
        decoded = decodeURIComponent(requestPath);
    } catch {
        return null;
    }
    const base = resolve(root);
    const full = resolve(base, `.${decoded.startsWith("/") ? "" : "/"}${decoded}`);
    return full.startsWith(base + sep) ? full : null;
}

// The longest mount whose prefix matches, and the rest of the path under it.
export function findMount(mounts: Mounts, pathname: string): { dir: string; rest: string } | null {
    const prefix = Object.keys(mounts)
        .filter((p) => pathname.startsWith(p))
        .sort((a, b) => b.length - a.length)[0];
    return prefix === undefined ? null : { dir: mounts[prefix], rest: pathname.slice(prefix.length) };
}

export function startServer(mounts: Mounts): TestServer {
    const server = Bun.serve({
        port: 0,
        hostname: "127.0.0.1",
        async fetch(request) {
            const mount = findMount(mounts, new URL(request.url).pathname);
            const path = mount && resolveWithin(mount.dir, mount.rest);
            const file = path ? Bun.file(path) : null;
            if (!path || !file || !(await file.exists())) {
                return new Response("not found", { status: 404 });
            }
            return new Response(file, { headers: { "content-type": contentType(path), "cache-control": "no-store" } });
        },
    });
    return { url: `http://127.0.0.1:${server.port}`, stop: () => server.stop(true) };
}
