# Native docking

Start at `docking.cppm` (`phoenix.gameplay.docking`). The module owns DockPortState, DockingState and DockConnection continuation plus their unchanged JSON shape. `docking.cpp` owns port rotation, 3D capture envelopes, mating/separation progress synchronized body motion, capture momentum and the propulsion phase continuation. The latter owns captured root velocities and mass compensation without retaining ECS references.

`guidance.cpp` independently compiles assisted port approach, hull detours and latched inertial alignment, consuming kinematic/collider values and flight limits. Report visibility stays an explicit admission guard in the composition adapter.

Inputs are compiled port definitions, kinematic snapshots and narrowly scoped permission/working/crossing facts. The module has no Flecs or World dependency; it uses world-state value contracts and content logistics definitions.

`src/world/bindings/gameplay_docking_01.cpp` and `gameplay_docking_02.cpp` are the ordinary World composition adapters for authoritative commands, guidance commitment, port/mission permissions, root membership and integration with physical batch/team crossings. It applies native transition results immediately and retains the original bounded topological synchronization passes. No state reference may survive an ECS archetype-changing operation.

Relevant checks: docking, craft, cargo, boarding and native/WASM continuation. Real docking UI journeys are separately invoked smoke evidence.
