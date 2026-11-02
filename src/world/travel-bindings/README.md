# Travel integration

Start with [docking_admission.cpp](docking_admission.cpp). Docking, finite cargo/supply, craft sorties, FTL departure and waypoints/navigation intent.

This is private native integration in `phoenix.simulation:travel_bindings`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.

Departure bindings consume physical-consumer grant facts from the Controller owner, partitioned by command ownership. The InterdictionBook owns encounter participation and capture rules for departure permission and obsolescence; World supplies operational-owner lookup.

Docking, logistics and FTL observations are captured as recipient-scoped portable records in `phoenix.knowledge.decision_travel`. The board methods serialize those same records for presentation; Controller decisions consume the typed captures directly. Capture presence is distinct from a fitted mechanism or an empty permitted feed.

Craft reports and own-craft guidance are captured directly as recipient-scoped `CraftDecisionRow` / `CraftSelfDecision` records owned by `phoenix.knowledge.decision_operations`. Presentation boards serialize the same capture; own-craft armed status uses only the permitted typed combat snapshot.
