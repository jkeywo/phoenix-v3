# Security teams and boarding

Security uses the generic ship-authored team System. Ordinary away missions can require or optionally accept a `capability: "security"` team slot. Teams have whole-team conditions `healthy`, `injured`, or `lost`; `injured_effectiveness_permille` controls their authored penalty. Medical treatment restores injured teams through the existing clinical work mechanism. Lost teams cannot be healed or reassigned.

## Boarding operations

A target hull authors `boarding_missions`. Dispatch discovers its templates when the target is detected and belongs to another operational owner/faction. The template supplies `id`, `title_key`, an entry compartment, an away tree with `boarding: true`, a conversation tree, and ordinary away slots. At least one required security slot is mandatory. Each contributing Console retains its existing Controller authority. Optional Medical reduces an encounter's nonfatal injury probability, without reducing its authored loss probability.

Transport uses existing manifest staging, capacity and transfer ordering:

- Displacement retains its qualification, range, power and obstructing shield checks.
- A `kind: "transport", mode: "shuttle"` bay permits boarding only when total target compartment HP is strictly below 50% of total maximum HP. Manually flown own-ship boarding uses the same health gate. Recovery does not reapply that insertion gate.
- `mode: "boarding_torpedo"` bypasses the shield/HP insertion gates. The torpedo is spent once launched and never returns. Airborne cancellation is refused; after delivery the team must bind another transport for extraction.

Physical bays author `craft`, finite `count`, `range_mm`, `capacity`, `transfer_ticks`, `compartment`, `arrival_distance_mm`, `arrival_speed_mm_s`, and `cruise_speed_mm_s`. Craft are ordinary inertial entities with real collision, flight Controllers, sensors, resources and optional combat structure. Legacy fittings compile into ordinary craft/bay definitions. Their dedicated Follow command runs through an ordinary Controller policy using the shared target and altitude observations. Approach ports include hull clearance; authored arrival distance and speed still gate transfer and docking. Actual proximity and relative speed are required before transfer finishes. Disabled or destroyed craft cannot teleport their passengers to the target. A destroyed occupied craft loses its manifest.

`retain_craft` defaults to true on missions/templates. A retained shuttle stays committed at the target for recovery. With false it physically returns empty after delivery. Binding a different extraction transport releases the retained craft to fly home; finishing mission work alone does not free a retained shuttle. Cancelled airborne shuttle insertion returns the passengers physically before releasing their reservation. Boarding torpedoes can be cancelled only while queued before launch; launched torpedoes cannot return or be reused.

## Authored routes, clashes and outcomes

Boarding adds observations `arrived`, `encounter_done`, `encounter_success`, and `defenders` to the ordinary away vocabulary. Its additional literal-text actions are:

| Action                 | Effect                                                                       |
| ---------------------- | ---------------------------------------------------------------------------- |
| `move_to(compartment)` | Travel along the target's existing compartment connections.                  |
| `encounter(id)`        | Travel to and resolve a named encounter once.                                |
| `secure(objective)`    | Secure the objective after arrival and local resistance is overcome.         |
| `release(objective)`   | Relinquish an occupied objective.                                            |
| `capture(objective)`   | Transfer whole-ship operational authority after securing a bridge objective. |

Encounters author their compartment, actual defending security team IDs, `success_permille`, `team_bonus_permille`, `defender_penalty_permille`, `injury_permille`, `loss_permille`, `medical_reduction_permille`, `assessment_ticks`, and an optional conversation `report` line. Optional `defences` maps fitted System IDs to penalties: automated defences or psychic equipment can use the same model, with their real enabled/fault state affecting the clash. Associated officers can have separate authored injury/loss probabilities. Defenders travel through the same compartment routing as onboard teams and cannot simultaneously serve an away assignment. Saved encounter receipts prevent repeated tree actions or reloads from rerolling a resolved clash.

An assessment includes its contributing factors and reaches Dispatch only after its Comms message is delivered. Link loss leaves the existing conversation delivery and timed-default behavior in force. The mission decides which objectives to attempt and what reaching the reactor means; no global reactor decision overrides its Comms branches.

Objectives author `compartment` and `kind`:

- `bridge` permits whole-ship capture; `prize_crew: true` keeps the manifest committed aboard.
- `hold` names a fitted `system`. Its real resource operation is inhibited while surviving security physically holds that compartment without fresh opposing security.
- `sabotage` names a fitted `system` and repairable authored `fault`. Securing it activates that fault through ordinary damage/repair state.

Capture changes faction/operational ownership, revokes prior Console claims and Controller generations, and exposes the prize's Consoles to the capturing crew through the existing local/fleet lease paths. Original officers and teams are detained rather than automatically recruited. Capture does not grant partial Console access before the bridge is secured.

## Playing and authoring

Choose **Boarding action** with **Courier — Rescue watch**. Helm opens the target operation in Away missions. Tactical contributes Security and can bind a shuttle or boarding torpedo; Support may contribute Medical. Helm may instead bind displacement, lowering obstructing shields through normal controls. Comms chooses bridge seizure or reactor occupation. Hull damage and the chosen insertion method determine when delivery can proceed. Arrange actual extraction for surviving teams unless a prize crew remains committed.

Workshop exposes target templates, slot filters, encounters, objectives, craft fittings and the boarding tree vocabulary. Verification checks compartment/System/fault references, supported transport fits, probabilities and timed Comms defaults.

## Persistence and verification

Away save schema 9 preserves physical craft commitments and return state, boarding routes, assessments, encounter receipts, secured objectives, capture and individual-bay passenger state. The enclosing World schema is 15 and ordinary craft continuation schema is 2. Older ordinary-craft and displacement saves retain their supported migrations. Saves containing the removed legacy physical-craft roster or commitments require a new game; they are rejected rather than resetting missions or replenishing torpedoes. Legacy transport content remains supported through compilation into ordinary craft and bays. New team conditions and detention fields default compatibly on older saves.

The synthetic `security` integration case covers generic team treatment, permanent loss, target-authored capture, optional Medical, deterministic continuation, shuttle damage gating, physical delivery/return, and disposable torpedoes. The CI gate runs the same source natively and in WASM and compares continuation traces. These checks establish behavior, not production encounter balance. Native/browser UI smoke evidence and physical-device accessibility acceptance are recorded separately.

Verification on 22 October 2026:

- All 31 native CTest integration cases passed, together with the native build check.
- WASM Security, Medical, away, Personnel, combat and Controller cases passed. Native/WASM Security, Medical, away and combat continuation traces matched.
- The production pack verified 21 entities, seven maps and 14 Consoles. Changed C++, JSON, Markdown, YAML and CMake formatting checks passed.
- The manually invoked native boarding journey passed Tactical/Support contributions, physical torpedo delivery, a Comms objective choice, delivered assessment and prize Console access. Its bounded smoke variant sets encounter success to certainty and disables lethal clash losses; it does not establish shipped encounter balance. Captures are `boarding-manifest.png`, `boarding-choice.png`, `boarding-assessment.png` and `boarding-prize.png` in the execution directory.

The graphical run used Linux, Xvfb, software OpenGL and two 1280 × 900 Console regions. Browser UI, screen readers, physical input devices, audio output and six-display performance were not exercised by this run.

```sh
cmake --build build/linux --target phoenix_boarding_smoke
(cd build/linux && xvfb-run -a -s '-screen 0 2560x1080x24' ./phoenix_boarding_smoke .)
```

Physical boarding transport now shares the ordinary craft roster, serial bay operations, flight, damage, radio reports, capture ownership and FTL handling. Boarding torpedoes use the `boarding_torpedo` craft role and terminal `consumed` phase. After physical launch, no return, pickup, cancellation or FTL recall can reuse them. Legacy fitting IDs remain admission aliases that select the lowest-ID available craft and bind its canonical identity.

## Transport consolidation verification — 22 October 2026

Legacy shuttle and torpedo fittings now compile to the ordinary craft roster, bay lifecycle and Controller execution. Their Follow command retains the existing flight implementation. All transports share mission deployment/recovery finalizers and boarding insertion checks; transport authority uses the existing System-enabled Controller owner rather than the telemetry reader. Clash resolution and objective contention share defender availability.

- All 33 native integration cases and the native build check passed. Production verification passed for 30 entity definitions, 8 maps and 20 Consoles; the development pack passed for 18 entity definitions, 9 maps and 12 Consoles.
- WASM Security, craft, away, Medical and Controller runtime cases passed. All five native/WASM continuation comparisons matched exactly. Synthetic peer/restore comparisons reuse identical fixture ZIP bytes so packaging timestamps cannot change content identity between participants.
- The explicitly invoked native boarding journey, carrier shuttle/sortie journey and manually flown ship-transport journey passed using software OpenGL under Xvfb, with two 1280 × 900 Console regions. Prize and recovery captures were inspected. Boarding captures are under `build/linux/boarding-*.png`; carrier captures are under `build/linux/build/carrier-*.png`. These runs retain the balance, device, screen-reader and performance limits described above.
- Changed C++ formatting, JSON/Markdown formatting and whitespace checks passed.

Regression coverage includes legacy aliases and individual craft at the strict boarding-health boundary, own-ship boarding, retained Medical care and nonzero boarding entries across transports, queued cancellation, airborne torpedo refusal, consumed stock and target loss, alternate extraction, capture ownership, distinct telemetry readers and bay operators, absent defenders, supported older saves and explicit legacy-roster rejection. World schema 15, craft schema 2 and Away schema 9 preserve the unified continuation; legacy physical-craft saves require a new game.
