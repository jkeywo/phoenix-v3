# FTL reserve and departure facts

Start at `ftl.cppm` (`phoenix.gameplay.ftl`). `reserve.cpp` owns reserve preparation, charging/ready loss requests, off-state discharge and physical readiness/blocker interpretation. FtlReserve owns the native discharge remainder; MissionDeparture owns the frozen report record.

The interface consumes ShipResources/ResourceNetwork and explicit equipment facts. It does not query a World, choose an operator, authorize a departure or inspect hidden mission state. Definition and resource accounting modules are its only runtime dependencies.

`coordinator.cppm` (`phoenix.gameplay.ftl.coordinator`) and `coordinator.cpp` own departure groups, reassignment/pruning, capture cancellation, exact group continuation and the transient request queue. Readiness/authority and physical departure commits remain outside that owner.

`ftl.hpp` remains the World composition adapter for Console admission, displacement/visitor facts, group authorization, recall, report capture and deliberate departure. Those operations must remain at their current phases; the reserve module does not introduce a new scheduler or completion owner. Finite process readiness still derives from the actual reserve after payment/completion effects.

Relevant checks: FTL and finite-process synthetic integration/continuation, mission/debrief and recovery cases. This guide is not executed acceptance evidence.
