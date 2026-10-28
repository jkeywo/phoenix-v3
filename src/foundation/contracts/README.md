# Shared game and observation records

Start with `contracts.cppm` (`phoenix.contracts`, target `phoenix_contracts`). It defines stable IDs, operating commands, permitted observations and shared scene/display records. `contracts.cpp` and `serialization.cpp` own out-of-line operations and canonical JSON conversion for those records.

Contracts depend on portable data values, not catalogue compilers, Flecs, sessions or UI. A shared record must express a cross-boundary contract; native domain internals belong in their domain owner. Observations are already permission-filtered inputs, not an invitation for a presentation consumer to look up hidden authority.

Input, replication, storage and Console integration cases validate these records through real producers/consumers. Preserve numeric values, defaults and continuation/wire semantics. Consult migration evidence for executed schema/trace and browser gates.
