# Sensors

Start with `state.cppm` for retained acquisition and track records, then
`envelopes.cppm` for conservative reach profiles. `SensorEnvelopes` owns a derived
cache for one loaded World. Its callers supply an immutable region span and the
current sensor origin/range. Loading a World replaces the cache; it is never saved.

The envelope implementation uses fixed-point World geometry and scenario region
geometry, without ECS access or console/session access. It retains the adaptive
sector refinement and conservative legacy-client minima. Native sensor queries,
earned evidence, and restore checks are still being extracted from `sensors.hpp`.
Use the existing sensor/environment and continuation system cases for validation.
