# Flight mechanisms

`phoenix.flight` in `src/flight/flight.cppm` and `flight_definitions.cpp` supplies the shared typed flight definition and production compiler, without simulation or graphics dependencies. `src/simulation/flight.hpp` and `flight.cpp` supply per-fitted-System runtime execution inside `phoenix.simulation`. They use the existing integer direction/vector math and body state. The adapter neither integrates position nor resolves collision, and cannot fetch world or contact truth.

## Authoring

A flight spec selects `kind`: `engine`, `steering`, `lateral`, `boost` or `impulse`. Numeric distances/speeds/acceleration use mm, mm/s, mm/s²; headings/rates use millidegrees and millidegrees/s. Resource rates are authored units per second. The simulation rate is 20 Hz.

Common fields are `max_speed_mm_s`, `reverse_speed_mm_s`, `acceleration_mm_s2`, `turn_mdeg_s`, `lateral_speed_mm_s`, `lateral_acceleration_mm_s2`, `assist_lateral`, `power_rate`, `heat_rate` and `match_stale_ticks`. Reverse defaults to zero; lateral cancellation is explicit. Engine/boost/impulse require positive forward speed and acceleration; steering requires positive turn rate; lateral requires positive lateral speed and acceleration. Each fitted actuator has its own state, demand and grant.

An engine may additionally define `boost:{acceleration_multiplier_permille,speed_multiplier_permille,power_rate,heat_rate}` and `impulse:{charge_ticks,acceleration_multiplier_permille,speed_multiplier_permille,steering_permille,charge_power_rate,power_rate,heat_rate}`. A standalone boost/impulse uses the same mode fields at its root, with its own speed and acceleration. Compiler limits apply after multipliers, so authored values cannot exceed supported integer arithmetic. Combined power/heat rates remain finite.

## Commands and resources

`apply_flight` validates a typed command against the fitted capability before changing a copied state. Speed supports the authored reverse limit. Direct longitudinal and steering axes require manual mode; lateral input requires a fitted lateral actuator. Assistance initialises speed from the current forward velocity and heading from the current attitude, and clears held controls. Throttle/manual input cancels Match Speed; steering does not.

`observe_flight` optionally accepts an already permitted contact observation. Call it before `flight_demand` to quote the correct current target. The demand reports actual required effort and power/heat rates. The resource owner grants a fraction in 0..1000; `step_flight` scales actual acceleration, turn, heat and charge progress by that fraction. A zero grant cannot create thrust or charge. The adapter does not allocate energy or coolant itself.

Assisted longitudinal control regulates only the forward velocity component. It never overwrites unrelated lateral velocity. Manual lateral release preserves drift; optional authored lateral assistance physically cancels it through the lateral actuator. Steering changes attitude without rotating velocity.

Boost adds sustained forward thrust within its authored speed limit. Impulse progresses from idle through paid charging to active forward transit. Charge completion enables active thrust on the following tick, avoiding active propulsion charged against a charging-only grant. Cancelling impulse clears charge progress and leaves velocity intact; ordinary assisted braking may subsequently change that velocity through paid acceleration. Boost and impulse are mutually exclusive within one actuator.

For separately fitted boost, impulse and steering Systems, the ship adapter must enforce the same exclusive boost/impulse policy and set each steering state's `steering_limit_permille` to the active impulse limit before quoting/executing steering. This is an explicit cross-System constraint, not hidden world lookup in the actuator.

## Match Speed knowledge

`FlightContactObservation` contains only contact identity, observed speed, observation tick and uncertainty. The adapter updates a matched target only from a newer observation of the same selected/matched contact, never from future observations or hidden velocity. It retains the last observed speed after the stale threshold and exposes staleness. Changing the selected contact cancels matching rather than silently following another actor. Restoring sight of the same contact refreshes an active match.

## Handover and continuity

`release_flight_inputs` clears held longitudinal/lateral/steering axes and boost without resetting velocity, current settings, observed knowledge or impulse work. Mode changes and command application never integrate position. Root authority routing invokes this on device/route loss and handover.

`save_flight` records all continuation fields, including signed integration remainders, observation metadata, charge progress and steering limit. `load_flight` checks the authored-definition signature, schema, exact types/ranges and consistent modes before returning a complete replacement state. A failed load leaves the caller's existing state untouched. Root world restore additionally validates saved observation ticks against the world clock and recovers live claims separately.

Synthetic integration should exercise resource-limited acceleration/charging, signed reverse/lateral thrust, steering without velocity rotation, boost/impulse release preserving momentum, stale Match Speed and resumed continuation. Swept collision handling and production world/Console admission remain owned by their existing runtime paths; they are separate from this actuator adapter.

Nonzero fractional effort conservatively rounds its authored energy and heat rates upward to the next rate unit; payment scaling uses the same rule. This prevents prolonged tiny inputs from producing motion while every debit truncates to zero. The resource owner still retains its per-tick rate remainders.

## Swept collision integration

The existing collision unit also exposes `sweep_collide(a,b,start_a,start_b)`. The world builds broadphase bounds spanning each body's start and predicted end, preserving stable pair order. The sweep evaluates the actual circle/rotated-box separation over relative translation with the current tick's fixed orientation, finds first penetration at deterministic billionth-tick precision and reuses the existing collision response. It stops the movement segment at contact; tangential velocity continues on subsequent ticks. Position integration and objective crossings consume the resulting executed segment.

The minimum search uses convex squared circle distance, circle-to-box closest distance or maximum separating-axis gap. Bounded fraction refinement replaces travel-distance substeps, so a thin obstacle is not skipped by an arbitrary substep cap. No scene/render shape substitutes for the authored collider. `tests/system/swept_collision.cpp` exercises all supported shape pairs, rotated thin walls, opposing dynamic actors, a true broadphase near miss and recovery through real production-loader Worlds. Native/WASM execution and graphical journeys are recorded by the root verification stage.
