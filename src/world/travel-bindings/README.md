# Travel integration

Start with [docking_admission.cpp](docking_admission.cpp). Docking, finite cargo/supply, craft sorties, FTL departure and waypoints/navigation intent.

This is private native integration in `phoenix.simulation:travel_bindings`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.
