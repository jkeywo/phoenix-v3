# Combat integration

Start with [combat_execution.cpp](combat_execution.cpp). Directed and projectile execution, physical collision/damage/repair hooks and combat continuation.

This is private native integration in `phoenix.simulation:combat_bindings`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.
