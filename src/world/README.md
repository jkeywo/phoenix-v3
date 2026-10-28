# Authoritative World composition

Start with [the public World contract](lifecycle/simulation.cppm), then [the fixed tick sequence](schedule/schedule.cpp). World owns actor lifetime, authoritative phase order and atomic candidate replacement. The public facade does not expose the private Runtime.

Read [physical state](state/README.md), [spatial queries](spatial/README.md), [native actor assembly](native-actors/README.md), [checkpoint composition](persistence/README.md) and [scheduling](schedule/README.md) for the core. Domain integration is grouped by the workflow being changed:

| Workflow                                                        | Owns                                                                                                                                                               |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| [Actor core](native-actors/README.md)                           | Native instance construction, compiled entity creation, capability indexes and generic equipment actuation.                                                        |
| [Command routing](command-routing/README.md)                    | Native command dispatch, Controller execution/generations, coordination, bridge operations and control refresh.                                                    |
| [Resource integration](resource-bindings/README.md)             | Power/processor preparation, physical stock bindings and permitted resource publications. Definition-driven processor row formatting belongs to ResourceProcessor. |
| [Effect application integration](effect-applications/README.md) | Prepared transactions, logical-to-native target binding, custom/property application, payload capture, native outcomes and damage hooks.                           |
| [Effect lifecycle integration](effect-lifecycle/README.md)      | Authored rule sources, clock/lease qualification, upkeep quotes/commit, region triggers and continuation binding. RuleApplications owns canonical evaluation.      |
| [Effect work integration](effect-work/README.md)                | Ability activation ports, process execution/funding, physical machinery and finite-work receipts. AbilityLifecycleBridge is private to this workflow.              |
| [Combat integration](combat-bindings/README.md)                 | Directed and projectile execution, physical collision/damage/repair hooks and combat continuation.                                                                 |
| [Crew integration](crew-bindings/README.md)                     | Personnel, team orders, Away admission/projection, boarding/capture and conversations. Independent domain owners retain their canonical runtime state.             |
| [Travel integration](travel-bindings/README.md)                 | Docking, finite cargo/supply, craft sorties, FTL departure and waypoints/navigation intent.                                                                        |
| [Sensor integration](sensor-bindings/README.md)                 | Physical sensor queries and admitted scanning, psychic apparatus and permitted signal projections.                                                                 |
| [Scenario integration](scenario-bindings/README.md)             | Scenario actor spawn/effect/publication lifecycle, streamed environment and interdiction coordination.                                                             |
| [World projections](projection-bindings/README.md)              | Console sessions/observations, public scene/debug views and authorized display publication.                                                                        |

These folders are private partitions of the same simulation module, sharing canonical Runtime identity and explicit field/query lifetime. They are reading/compilation seams rather than independent authoritative libraries. `lifecycle/runtime.cppm` declares their common private state; the public World facade never reexports it. `phoenix_world_integrations()` composes the folder-local source lists after the target exists.

Native command envelopes use the shared [shape policy](../control/command-shapes/README.md); domain workflows retain all authority, generation, supported-value and physical checks. Processor presentation comes from its resource owner after native permitted-feed filtering. Core scheduling does not interpret a data-defined phase order.

See [the refinement record](../../docs/understandability-refactor.md) for the delivered split, code growth, checks and remaining coupling. Full native/browser/system gates and manually invoked graphical evidence have separate scope.
