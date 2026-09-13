import { readFileSync } from "node:fs";
import { join } from "node:path";
import { FILES_DIR } from "./env";

export function baseConfig(): any {
    return JSON.parse(readFileSync(join(FILES_DIR, "shipofharkinian.json"), "utf8"));
}

// The base config set up to play: debug mode off, because with DebugEnabled choosing file 1
// opens map select instead of loading the save; and SDL audio named explicitly, because a
// desktop config names a backend this build may not have (see audio.test.ts).
export function playConfig(): any {
    const config = baseConfig();
    config.CVars.gDeveloperTools = { ...config.CVars.gDeveloperTools, DebugEnabled: 0 };
    config.Window = { ...config.Window, AudioBackend: "sdl" };
    return config;
}

export const playFiles = (config: any = playConfig()) => ({ "/shipofharkinian.json": JSON.stringify(config, null, 4) });

// What an embedder hands over from someone's desktop install: an audio backend this build
// does not have, an enabled mod with no file, and Settings reopening on the Mod Menu.
export function staleDesktopConfig(): string {
    const config = baseConfig();
    config.Window = { ...config.Window, AudioBackend: "coreaudio" };
    const settings = (config.CVars.gSettings ??= {});
    settings.EnabledMods = "a-mod-that-is-not-here";
    settings.Menu = { ...settings.Menu, SettingsSidebarSection: "Mod Menu" };
    return JSON.stringify(config, null, 4);
}
