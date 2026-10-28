# Physical values and fixed arithmetic

`state.cppm` exports `phoenix.world.state`; link `phoenix_world_state`. Read the `fixed` namespace first, then StableId, Pose, Motion, collision records and vertical/native body state. `state.cpp` owns out-of-line conversions/serialization attached to these records.

The dependency points down to foundation contracts. These values do not own a Flecs registry, content catalogue or tick scheduler. Domain services borrow/update them, while World owns their component lifetime. The shared simulation rate and integer unit conventions are part of the cross-module contract.

Motion, geometry, flight and save/restore integration cases exercise these types through production paths. Preserve arithmetic boundaries and JSON defaults; migration evidence distinguishes compilation from the exact continuation checks.
