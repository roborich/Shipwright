# Scene asset headers

These headers are static, committed files — Torch has no OoT header exporter, so
nothing regenerates them. Each symbol pairs a C identifier with the `__OTR__`
archive path used to look the resource up at runtime.

Since the stable-names change, archive resources use offset-free names taken
from the yml trees (`soh/assets/yml/*/scenes/`). Only the path strings of
symbols actually referenced from C were updated to match; the remaining
offset-named symbols keep their legacy `__OTR__` paths, which no longer exist
in extracted archives.

If you start referencing one of these symbols from C, check its path's final
component against the corresponding yml first and update the `#define` if it
still carries a ROM offset. A stale path compiles fine and only fails at
runtime, as a missing-resource lookup.
