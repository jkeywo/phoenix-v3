# Command routing

Start with [world_commands.cpp](world_commands.cpp). Native command dispatch, Controller execution/generations, coordination, bridge operations and control refresh.

This is private native integration in `phoenix.simulation:command_routing`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.

`decision_capture.cpp` owns the shared synchronous frozen/fallback capture, recipient restriction and retained-inspection enrichment. Its private capture record owns optional replacements and borrows frame inputs otherwise; no snapshot or Controller reference is retained across admitted entity transitions.

`decision_boundary.cpp` adapts explicitly supplied provider/debug `Observation` inputs into `DecisionObservation` using canonical domain decoders. This is an explicit serialization boundary; ordinary frozen and fallback captures populate typed records directly and never call it. The boundary uses only the supplied permitted snapshot, with no raw-world enrichment.

Controller availability projection decodes the current Console observation once for its Controller rows. Each Controller still applies its own recipient restriction and inspection evidence to that immutable input. The decoded view lives only for that projection.
