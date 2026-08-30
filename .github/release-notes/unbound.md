**SoH: Unbound** — Ship of Harkinian with the N64-era modding limits removed (uncapped collision, a growable scene/entrance registry, growable text, and the JSON scene format that merges across mod layers). The format contract is [`unbound-docs/SPEC.md`](https://github.com/roborich/Shipwright/blob/unbound/unbound-docs/SPEC.md).

**Format version 2** (from 9.2.3-unbound0.3): collision and room-mesh vertices are 32-bit integers, so one scene may span the whole world extent. Mods exported for format version 1 (any pre-0.3 export) are refused with an `[Unbound]` log line and must be re-exported; see `unbound-docs/prelude-handoff.md`.

**Setup**
1. Install like regular Ship of Harkinian and run it once with your ROM; it produces `oot.o2r` and then converts it to the Unbound base `oot-unbound.o2r` in the same folder (about a second). The base is regenerated automatically when the ROM archives or the build change.
2. Drop Unbound-format mods (`.o2r`) into `mods/`.

On macOS the game folder is `~/Library/Application Support/com.shipofharkinian.soh/`.
