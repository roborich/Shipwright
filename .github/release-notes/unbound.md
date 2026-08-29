**SoH: Unbound** — Ship of Harkinian with the N64-era modding limits removed (uncapped collision, a growable scene/entrance registry, growable text, and the JSON scene format that merges across mod layers). The format contract is [`unbound-docs/SPEC.md`](https://github.com/roborich/Shipwright/blob/unbound/unbound-docs/SPEC.md).

**Setup**
1. Install like regular Ship of Harkinian: run it once with your ROM to produce `oot.o2r`.
2. Generate the Unbound base archive next to `oot.o2r` (once per SoH version):
   `soh --export-unbound oot-unbound.o2r` from the game folder, or open the in-game console and run `unbound-export oot-unbound.o2r`, then restart.
3. Drop Unbound-format mods (`.o2r`) into `mods/`.

On macOS the game folder is `~/Library/Application Support/com.shipofharkinian.soh/`.
