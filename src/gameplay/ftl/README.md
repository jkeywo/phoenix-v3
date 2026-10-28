# FTL reserve and departure facts

Start at `ftl.cppm` (`phoenix.gameplay.ftl`). `reserve.cpp` owns reserve preparation, charging/ready loss requests, off-state discharge and physical readiness/blocker interpretation. FtlReserve owns the native discharge remainder; MissionDeparture owns the frozen report record.

The interface consumes ShipResources/ResourceNetwork and explicit equipment facts. It does not query a World, choose an operator, authorize a departure or inspect hidden mission state. Definition and resource accounting modules are its only runtime dependencies.

`coordinator.cppm` (`phoenix.gameplay.ftl.coordinator`) is the departure policy front door. `coordinator.cpp` owns groups, reassignment/pruning, capture cancellation, exact group continuation and the transient queue. `admission.cpp` resolves Console ownership from selected layout data, checks command shape and current Controller grants, orders group readiness, prepares the accepted batch and validates restored membership/authority. It owns no physical resource or World.

`src/world/bindings/gameplay_ftl.cpp` is the ordinary World adapter for bounded drive/Controller capabilities, displacement/visitor and cross-domain group permission facts, reserve charging, report capture and physical departure effects. Those operations must remain at their current phases; the reserve module does not introduce a new scheduler or completion owner. Finite process readiness still derives from the actual reserve after payment/completion effects.

Relevant checks: FTL and finite-process synthetic integration/continuation, mission/debrief and recovery cases. This guide is not executed acceptance evidence.

`DepartureAdmissionFacts` contains synchronous reads only. Its capabilities expose
current drive/Console ownership and Controller grants without AI memory or a
World getter. `prepare_pending` returns the whole accepted batch before World
dissipates any reserve, freezes reports, changes Away transfer state or withdraws
scenario actors. Restore validates members, performs the existing obsolete
encounter-goal cleanup, then validates authority in that order.

The owner and its ordinary native adapters passed the final native and
ordinary/threaded WASM project gates, including continuation comparisons.
See the [verification record](../../../docs/modularisation-evidence.md) for
executed checks and environmental limits.
