# Effect work integration

Start with [ability_lifecycle.cpp](ability_lifecycle.cpp). Ability activation ports, process execution/funding, physical machinery and finite-work receipts. AbilityLifecycleBridge is private to this workflow.

This is private native integration in `phoenix.simulation:effect_work`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.
