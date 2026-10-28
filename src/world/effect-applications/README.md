# Effect application integration

Start with [effects_preparation_adapter.cpp](effects_preparation_adapter.cpp). Prepared transactions, logical-to-native target binding, custom/property application, payload capture, native outcomes and damage hooks.

This is private native integration in `phoenix.simulation:effect_applications`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.
