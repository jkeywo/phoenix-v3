# Sensors

Start with `state.cppm` for retained acquisition and track records, then
`envelopes.cppm` for conservative reach profiles. `SensorEnvelopes` owns a derived
cache for one loaded World. Its callers supply an immutable region span and the
current sensor origin/range. Loading a World replaces the cache; it is never saved.

The envelope implementation uses fixed-point World geometry and scenario region
geometry, without ECS access or console/session access. It retains adaptive
sector refinement and conservative legacy-client minima.

`acquisition.cppm` owns sample progression, `findings.cppm` owns discovery state
and finding progression, and `evidence.cppm` resolves local/retained evidence.
`validation.cppm` encodes and validates scanner continuation using immutable
native identity/definition facts. `projection.cppm` builds the board from the
Console's permitted scanner feeds. Borrowed scanner/state pointers live for one
projection or restore call; returned continuation owns its values.

`src/world/bindings/gameplay_sensors.cpp` binds physical ECS queries, admission
and publication at the existing phases. It compiles through the private World
partition; the former contextual `sensors.hpp` fragment is retired. This native
integration policy remains explicit rather than exposed through an unrestricted
World interface. The final native and ordinary/threaded WASM project gates
passed, including sensor/environment cases and continuation comparisons; see
the [verification record](../../../docs/modularisation-evidence.md).
