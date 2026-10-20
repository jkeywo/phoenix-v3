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

The native synthetic combat case passes the existing contracts plus arbitrary magazine counts, unload/reload conservation, loaded continuation, constant paid beams and solid cover, layered shield bypass, off-state recharge, battery-mediated discharge, warm-up and team diagnosis. Full platform, ordnance, authoring and graphical acceptance remains in progress.
