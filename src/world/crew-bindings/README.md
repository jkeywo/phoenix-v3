# Crew integration

Start with [away_commands.cpp](away_commands.cpp). Personnel, team orders, Away admission/projection, boarding/capture and conversations. Independent domain owners retain their canonical runtime state.

This is private native integration in `phoenix.simulation:crew_bindings`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.
