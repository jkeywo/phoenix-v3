# Actor core

Start with [actor_assembly.cpp](actor_assembly.cpp). Native instance construction, compiled entity creation, capability indexes and generic equipment actuation.

This is private native integration in `phoenix.simulation:native_actors`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.
