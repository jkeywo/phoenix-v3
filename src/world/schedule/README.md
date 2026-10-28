# Tick composition and profiling

`schedule.cpp` contains the explicit `World::tick` sequence. Its private `phoenix.simulation:schedule` partition imports the canonical native Runtime; it is not reexported by the public facade. Domain owners still perform their own rules and state transitions. The schedule preserves the original order of command consumption, work/funding phases, motion, effects, observations and publication.

`profiling.cppm` exports the independently compiled `phoenix.world.schedule.profiling`; link `phoenix_world_profiling` for `TickProfile`. `profiling.cpp` records elapsed stage samples for explicitly enabled diagnostics. This module measures execution order and does not choose it.

The owner has no content, gameplay or renderer dependency. Timing samples are diagnostic output and never enter authoritative state, random decisions or continuation. World uses the named profiling owner rather than including a private implementation header.

Compilation verifies the profiling API. Performance captures are manually invoked evidence; no machine-specific timing threshold or profiling output establishes gameplay acceptance. Actual runs belong in the migration evidence.
