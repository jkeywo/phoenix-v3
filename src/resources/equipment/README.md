# Fitted equipment resource adapter

Start with `equipment.cppm` (`phoenix.resources.equipment`, target `phoenix_resources_equipment`). ShipResources and ShipResourceAllocation connect immutable per-fit bindings to the native ResourceNetwork. `gameplay_resources.cpp` owns request/resolve/consume, operating grants, heat and allocation-preview behavior.

This owner sits above resource definitions/network execution and below authoritative World composition. It must preserve one committed network tick: a preview cannot debit a second time, and a stale stock/selection snapshot cannot commit. Effect work and flight/process services use the same grants rather than inventing their own accounting.

`processors.hpp`, `supply.hpp` and `power_projection.hpp` still contain World integration code during migration; they are not public import interfaces. Existing stock, processor, paid-flight and resource-allocation cases cover the real adapter. The ledger/evidence records what has been extracted and which continuation/platform checks have run.
