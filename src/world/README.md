# Authoritative World composition

Start with `lifecycle/simulation.cppm` (`phoenix.simulation`) for the public World contract: load content, claim routes, submit commands, advance fixed ticks, observe permitted state and save/restore. `lifecycle/world.cpp` composes the shared Flecs registry and domain services. It owns actor lifetime, phase order and the final candidate swap during restore.

Read `state/` for physical records and fixed arithmetic, `spatial/` for broad/narrow collision work, `lifecycle/assembly.cppm` for immutable entity assembly, and `persistence/` for checkpoint envelope/roster validation. `schedule/` owns optional profiling; timings never drive simulation. Gameplay, effects, resources, control and knowledge own domain mechanisms below World.

The migration still contains native integration fragments included into World. Their presence in a family folder does not make them independent public modules. The private World outline and final native policy adoption remain separate work; consult the ledger and migration evidence rather than inferring completion from this guide.

Existing synthetic system journeys and exact continuation traces are the behavioral gates. Native, ordinary/threaded browser, graphical and accessibility evidence are recorded separately in `docs/modularisation-evidence.md`.
