# Command routing

Start with [world_commands.cpp](world_commands.cpp). Native command dispatch, Controller execution/generations, coordination, bridge operations and control refresh.

This is private native integration in `phoenix.simulation:command_routing`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.

`decision_capture.cpp` owns the shared synchronous frozen/fallback capture, recipient restriction and retained-inspection enrichment. Its private capture record owns optional replacements and borrows frame inputs otherwise; no snapshot or Controller reference is retained across admitted entity transitions.
