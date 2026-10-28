# Resource integration

Start with [resources_processors.cpp](resources_processors.cpp). Power/processor preparation, physical stock bindings and permitted resource publications. Definition-driven processor row formatting belongs to ResourceProcessor.

This is private native integration in `phoenix.simulation:resource_bindings`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.
