# Permitted projections and snapshot lifetimes

`phoenix.knowledge.projections` owns the frozen decision frame and presentation cache. `projections.cppm` defines the records and read/freeze interfaces; `projections.cpp` owns mutation and cache invalidation. It has no World, Flecs, graphics or device access.

World composition captures legitimate domain observations, then freezes command sections, Console evidence and addressed inboxes before Controllers run. Presentation is cached independently until a mutation. Neither cache is saved; restore reconstructs it. The remaining projection readers are pure functions over permitted records, shared with client/UI consumers during migration. Relevant integration: Controller runtime, knowledge scoping, session handover and continuation.
