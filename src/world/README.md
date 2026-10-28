# Authoritative World composition

Start with `lifecycle/simulation.cppm` (`phoenix.simulation`) for the public World contract: load content, claim routes, submit commands, advance fixed ticks, observe permitted state and save/restore. `lifecycle/runtime.cppm` declares the private native Runtime, `lifecycle/assembly_bindings.cpp` installs compiled entities, `schedule/schedule.cpp` lists the tick phases, and `persistence/checkpoint_bindings.cpp` coordinates the candidate restore and final swap.

Read `state/` for physical records and fixed arithmetic, `spatial/` for broad/narrow collision work, `lifecycle/assembly.cppm` for immutable entity assembly, and `persistence/` for checkpoint envelope/roster validation. `bindings/` groups native integration by domain. `schedule/` also owns optional profiling; timings never drive simulation. Gameplay, effects, resources, control and knowledge own independently compiled domain mechanisms below World.

Native integration is independently compiled as ordinary `phoenix.simulation` implementation units through private `:runtime`, `:bindings`, `:schedule` and `:persistence` partitions. The public facade does not reexport those partitions. The former class-body headers have been retired after complete body/field-order and reference audits. This compilation split does not establish that every remaining integration policy has reached its final domain owner; consult the bindings guide and migration ledger for that work.

Existing synthetic system journeys and exact continuation traces are the behavioral gates. Native, ordinary/threaded browser, graphical and accessibility evidence are recorded separately in `docs/modularisation-evidence.md`.
