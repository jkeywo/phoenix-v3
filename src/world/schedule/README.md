# Tick profiling

`profiling.cppm` exports `phoenix.world.schedule.profiling`; link `phoenix_world_profiling` for `TickProfile`. `profiling.cpp` records elapsed stage samples for explicitly enabled diagnostics. Read the World tick composition for execution order: this module measures that order and does not choose it.

The owner has no content, gameplay or renderer dependency. Timing samples are diagnostic output and never enter authoritative state, random decisions or continuation. World uses the named profiling owner rather than including a private implementation header.

Compilation verifies the profiling API. Performance captures are manually invoked evidence; no machine-specific timing threshold or profiling output establishes gameplay acceptance. Actual runs belong in the migration evidence.
