# Remaining MVP combat

This is the accepted follow-on plan to `codex/combat-cannons` (`d2e2f63`). It is an acceptance contract, not a completion claim. Build the existing Courier/pirate asteroid duel into the demonstration scenario; retain authored Console ownership and ordinary Controller AI.

## Accepted requirements

- Sustained beams: a visible line, constant authored damage per second, real continuous energy and heat limits, first-obstruction collision, persistent assignments.
- Generic magazine selection: any number of compatible magazines, one authored ammunition/fire-mode definition per magazine, timed unload/reload, stock conservation including space reserved for unloading. Preserve committed launch payloads and old single-magazine content.
- Homing missiles: long range, bounded independent seeker, curved elevated approach, high damage and shield bypass, physical AoE. No post-launch retargeting.
- Havok missiles: rapid fire, close range, lower damage, seeded chaotic unguided flight.
- Missiles: physical altitude and collision volumes, damageable durability, smoke trails, eight camera-relative sprite directions with hysteresis and one-axis billboard rotation to align with projected flight. Ship interior damage still selects a weighted deck independently of height.
- Point defence: Off/Auto/Manual modes; noticeable paid Auto standby; sensor-permitted hostile-missile selection by imminent threat; close-range manual ship assignments; shared turret/ammunition/heat budgets; rapid fast tracers with high penetration and low deposition so missiles take several hits. Cancel returns Manual to Auto.
- Shields: arbitrary overlapping hull-relative arcs with explicit layer ordering; bypass applied once; raised sustaining draw, powered recharge, off-state slow powered recharge preserving stored strength, warm-up exposing stored strength gradually. Power loss removes coverage while preserving reserve. Battery-mediated discharge and redistribution respect real capacity, energy conversion and transfer limits. Retuning is deferred.
- Equipment: specific authored faults without separate System HP. Effects change real capabilities; overheating, faults and zero-HP compartment disablement remain independent. Destroyed equipment cannot be field repaired in this encounter.
- Diagnosis: owners see supported symptoms; repair-team arrival reveals exact faults to Engineering and the owner. Clickable non-colour-only status lights open accessible detail popups without leaking undiscovered diagnoses.
- Repairs: Engineering can prioritise a specific System; otherwise choose worst affected reachable repairable System/compartment using disablement, severity, damage fraction, travel cost and stable identity. One team per fault, concurrent work on different faults, existing structural diminishing returns, unlimited supplies. New damage invalidates older repair work.
- Shared entity destruction component: authored probability, visual/audio and independent damage profile, once-only bounded deterministic chains. Missile destruction has no shield bypass and excludes missiles; impact/proximity warheads retain their separate payload. Ships gain damage-capable explosions while retaining their first-stage destruction trigger and visual timing. AoE respects physical distance, falloff, solid cover and faction-independent collateral.
- Presentation: smoke, beams, tracers and shield-hit feedback; bounded histories, reduced motion and no reconnect emission backlog. Keyboard/touch/controller actions, equivalent screen-reader semantics and shared localisation.
- Workshop: edit and verify every new definition through existing Undo/Redo, Verify and isolated Test. No runtime JSON interpretation.
- Persistence: save magazine/load commitments, seekers, randomness, shield transitions, faults/diagnosis, repairs and once-only destruction state. Native/WASM deterministic continuation; reject incompatible saves clearly.

## Delivery and verification

1. Shared damage/types/destruction and fault foundations.
2. Shields, redistribution, inspection and repairs.
3. Magazine selection and continuous beams.
4. Physical missiles and PDC interception.
5. Presentation, authoring, controls and the expanded duel.

Each verified logical increment is committed. Synthetic fixtures use production loaders, authority, resources and Flecs paths, independent of shipped balance. Cover success, refusal, interruption and continuation for each cross-system contract. Run native/WASM gates, content verification and formatting. Trigger and inspect native/browser graphical journeys separately; record actual screen-reader/device/six-display acceptance only when exercised.

After implementation, an independent agent reviews against this original contract. Confirmed gaps are fixed and reverified before completion.

Deferred: finite repair supplies/parts, full component simulation and patches, shield retuning, missile retargeting, fabrication and a new Cruiser scenario.

## Current evidence

The runtime, Workshop fields, Console controls and expanded duel are implemented. An independent agent reviewed the implementation against this contract; confirmed gaps were corrected and re-reviewed. The [verification receipt](smoke/combat-mvp.md) records exact platform gates, graphical journeys and acceptance limits.

## Runtime and authoring interface

Weapons accept `family: "beam"` with `damage_per_second`, `range_mm`, `energy_per_second` and `power_store`, or the default projectile family. `magazines` is an ordered compatible-ID array; legacy `magazine` and weapon-level `projectile` remain valid. A magazine can own its `projectile`, `load_ticks` and `fire_ticks` to select an ammunition type and cadence. Switching a loaded weapon consumes the old magazine's load duration to unload, returns its reserved round, then begins the selected magazine's timed load. Disablement pauses the unload; reselecting cannot cancel the commitment. Missile magazines in the duel have finite stock; replenishment policies remain the first-stage choices.

Projectile data adds `guidance` (`none`, `homing`, `chaotic`), seeker/turn/arc/chaos parameters, `durability`, volume radius/resistance, `shield_bypass_permille`, blast/fuse radii, optional `tracer`, and `trail: "smoke"`. `destruction` is the shared chance/visual/audio/damage profile accepted on entities and ordnance. Missile destruction profiles must exclude missiles and set zero shield bypass. Blast distance is measured to exposed collider surfaces; weapon payloads and destruction payloads remain separate.

`kind: "shield"` adds arc/bearing/layer, capacity/initial reserve, warm-up, sustaining energy, raised/off recharge, energy per strength and discharge rate/efficiency, bound to a real power store. Covering layers use `(layer, stable ID)` order. Sustaining power has priority within each shield's paid grant; recharge uses the remainder. Discharge respects the battery's remaining charge allowance and headroom, including conversion losses. Fractional conversion credit and sub-energy rate budgets persist across saves; blocked batteries cannot bank discharge rate for a later burst.

Controllers add `magazine_select` (zero-based compatible magazine), `weapon_mode` (0 Off, 1 Auto, 2 Manual), `repair_system` (zero for automatic, otherwise the one-based sorted physical equipment roster), and `shield_discharge` (boolean). Existing `system_enabled`, weapon targets and compartment assignments retain their admitted authority paths. Target zero returns a manual PDC to Auto.

Fault definitions supply stable identity, symptom, diagnosis, trigger chance/threshold, severity, effect and repair work. Output impairment supports motion, sensing, weapons, turrets, shields and teams; other machinery uses explicit disabled/feed faults. Destroyed equipment is excluded from repair work. Exact diagnoses are projected only after on-site inspection.

Sprite frames optionally supply a nonzero two-dimensional `forward` axis. Missiles select their eight-direction frame with hysteresis, then align that axis to projected physical velocity. Smoke is bounded renderer state and resets across discontinuities; reduced motion suppresses smoke and shield pulses. Beam and tracer lines retain the physical firing information.

Save schema **10** records ammunition return reservations, missile/random state, shields, continuous damage remainders, faults, diagnosis, repairs and destruction commitments. Earlier saves are refused explicitly.

## Original-plan review

The independent review identified fractional shield conversion loss, immediate unloading, targeted repairs ignoring structural disablement, audio-only destruction omission, partial faults ignored by basic motion, and degenerate missile projection losing its orientation. These are corrected with persisted progress and focused synthetic regression cases. Re-review found no remaining confirmed defects. A subsequent WASM compiler warning exposed an incorrectly associated `else` in generated weapon/repair choices; braces and a Console-model regression correct that issue too, and the reviewer checked the correction.

## Combat audit cleanup

The follow-up refactoring series uses save schema 10; previous continuation formats are explicitly refused.

1. Ship death is owned by `Destructible`. One shared destruction definition and once-only event drive damage, visual and audio output. Ship defaults and hull removal after the growth phase are preserved; the shared authored probability applies to the entire event. Restoration rejects compartment/death disagreement. The synthetic destruction case asserts exactly one sound-bearing event and resumed continuation equality.

2. Observations and weapon execution share permitted sensor-contact enumeration. Targeting requests only those contacts and caches them per Console within preparation or firing; firing starts a fresh cache after resource allocation. Human observation projects combat JSON once. Existing synthetic sensor restrictions, automatic/manual interception, energy and continuation contracts pass.

3. Cannon compartment traversal and swept missile interception use one material-deposition calculation, including fractional resistance/deposition carry and stopping distance. Their geometry remains separate. Interception no longer rounds every crossing up to a resistance cost of one. Synthetic contracts cover repeated low-deposition hits, structural path lengths and resumed interception equality.

4. One repair candidate policy handles structural urgency, fault severity, irreparable equipment, explicit system priority and claimed faults. Both automatic destination selection and on-site selection consume it. Travel, on-site inspection and structural stacking remain separate. Synthetic repair, destroyed-system exclusion and continuation contracts pass.

5. Compiled definitions and runtime state use separate weapon, turret, magazine, repair and shield records in closed variants. Shared ammunition/reserve configuration uses composition. Only the active state is serialized and restored, with kind-specific bounds and rejection of foreign fields. The public telemetry vocabulary stays stable. Synthetic combat and continuation contracts pass, including active-state-only and foreign-state refusal checks.
