# Example: a two-line delta mod

Moves actor `0` of Hyrule Field (`spot00`) room 0, setup 0, to `[0, 100, 0]`. Everything else in
the room comes from the layers below (the converted base archive). No `$schema` is needed in a
patch layer; the loader finds it in a lower layer.

Package with any zip tool, keeping the paths, and drop the result in SoH's `mods/` folder:

    cd hyrule-field-actor-delta && zip -r ../hyrule-field-actor-delta.o2r unbound.json scenes

With `oot-unbound.o2r` beside `oot.o2r`, the log (at debug level) shows
`scenes/spot00/rooms/0.json: merging 2 archive layers` when the field loads.
