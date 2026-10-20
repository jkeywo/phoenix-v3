# Helm boost, lateral thrust and impulse

20 October 2026. Implements the agreed extension to existing fitted flight mechanisms. See [flight authoring](flight-mechanisms.md) and [the GDD](GDD.md#51-local-flight).

Boost supports explicit typed System/property targets for speed, acceleration and turn rate. Paid bonuses combine by strongest value. Shipped propulsion templates target assembly-local engine/steering fittings and retain independent local batteries. Workshop exposes target records, lateral direction, total charging energy and minimum reserve.

Two momentary lateral controls are direction-aware and neutral when both are held. AI evaluates both lateral corridors from permitted evidence, uses existing avoidance when neither is feasible, and counter-thrusts to arrest drift. Complexity delegates the lateral Controller independently of human forward propulsion and steering. Shipped trees publish avoidance intent through normal Controller messaging.

The shipped Simple profiles delegate lateral thrust to AI while retaining human Helm movement. Middle/Detailed retain human lateral control. The Cruiser's paired lateral fittings share one Controller channel, providing one port/starboard button pair and feeding the same command to both physical actuators.

Impulse spends authored energy during charging after checking the starting reserve. Charging pauses when unfunded. Active transit uses bound propulsion and reduced steering until cancellation, collision, disablement or insufficient power. Flight schema 5 rejects incompatible older continuations explicitly.

Validation uses synthetic production-loader scenarios for targeting, stacking, partial/unpaid bonuses, target power, directional/simultaneous controls, mixed human/AI Helm ownership, hidden-obstacle refusal, paid charging and recovery, collision cancellation/restart and invalid authoring/save versions.

Verification on 20 October 2026:

- Native game and Workshop build; base and training packs verify; all 22 native system integration cases pass.
- Full WASM verification gate passes, including the extended paid-flight scenarios and native/WASM continuation comparison.
- All eight real-network cases pass. The first gate caught a missing neutral lateral descriptor; restoring that admitted command while retaining two visible direction buttons fixes remote release. The earlier GM/lobby timeouts passed on the complete rerun after compiler activity finished.
- Repository formatting and whitespace checks pass. Native build/storage checks require execution outside the restricted sandbox on this host.

Graphical touch/gamepad, actual screen-reader use, tuning and physical bridge acceptance remain manually triggered evidence; none is claimed by these automated checks.
