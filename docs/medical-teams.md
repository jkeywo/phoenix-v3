# Teams, Medical and officer casualties

Teams are finite groups authored as ship Systems with `kind: "team"`. A type is an author-defined identity, not an engine enum. Capabilities select supported work; orders define how that work is offered and executed. The legacy `repair_team` spelling still loads as a repair-capable engineering team. Numeric System identities and saved physical routes remain stable.

## Authoring and authority

A team fitting supplies `type`, `capabilities`, a starting `compartment` and `orders`. Each order has `id`, localised `label`, `icon`, `target` (`compartments`, `damage` or `patients`), optional required `capability`, and a `tree`. Patient orders can filter `patient_states` (`minor`, `severe`, `stabilised`); shipped treatment orders select minor/unstabilised cases and recovery orders select stabilised casualties. Reusable equipment definitions can supply these fields. Personnel authors one team duty; an attached officer travels with it. The owning Controller and department retain their ordinary authority.

Order trees use `context: "team"`. Boolean observations are `arrived`, `work_done` and `eligible`; `tick` is the simulation tick. Supported argument-free actions are `move`, `diagnose`, `repair`, `treat`, `stabilise`, `recover` and `complete`. The last emitted activity remains active until another action replaces it, including when authored as an entry action. Onboard and away teams step the same order runner once per active simulation tick, including travel; clinical work requires physical arrival. Repair and clinical actions require their respective team capabilities; invalid compositions fail verification. Ordinary state-tree inheritance and reference calls are available.

`team.order` commands identify the team in `system`, its observed revision in `value`, the owning Controller and generation, and `order/compartment` in `text`. Append `/officer` to prioritise an individual patient within that compartment. `team.cancel` uses an empty text. Human and AI commands share admission. Controller trees that call team commands through references enable `team_commands: true`; direct calls enable the vocabulary automatically. `team_revision` is an ordinary permitted System observation. Unused team vocabulary does not change legacy Controller continuation fingerprints. The host rechecks ownership, revision, eligible target, route and away reservations when executing. A team cannot simultaneously serve an away manifest and onboard work.

Select a team's order in the Console controls, then select a compartment on the map. Damage and patient markers follow that order's target filter. Clinical owners receive patient names and conditions; other departments retain their roster boundaries. System status indicators show a medical cross and a spoken condition when an associated officer is affected.

Workshop exposes team fitting fields, orders, medical traits, facilities, starting injuries, casualty policies and away-slot filters through the ordinary draft/Undo/Verify/Test flow. Its tree vocabulary includes the team context.

## Officer condition and treatment

Healthy officers supply their existing trait bonuses and qualifications after arriving at duty. A stun suppresses those contributions for 20 simulation seconds. Minor injury suppresses them until healed. Severe injury makes an officer unavailable; field Medical stabilises it, and a facility completes recovery. Officers and teams are separate from human player participation, and baseline machinery remains usable without officer expertise.

Medical teams use `capabilities: ["medical"]`. `treatment_ticks` and `stabilise_ticks` author the work required. A `medical` officer bonus changes treatment rate through the existing personnel modifier formula. A leaderless team remains functional. One patient-matching policy applies order filters before action restrictions and triage. Treatment accepts minor and unstabilised severe casualties; stabilisation accepts unstabilised severe casualties; recovery accepts stabilised severe casualties. Compartment triage selects unstabilised severe casualties before minor injuries; an individual priority breaks ties within severity. Excluded patients neither receive work nor block an order’s completion. Progress belongs to the patient and survives interruption. Routine injuries do not deteriorate automatically.

`personnel.facilities` contains records with `id`, `compartment`, `capacity` and `recovery_ticks`. A recovery order physically escorts a stabilised patient through compartment links before admitting them. Full or structurally disabled facilities cannot accept patients; structural disablement pauses recovery. Cancelling an escort leaves the patient at their actual location. Teams and facility places are finite. Medical consumables are deferred.

## Damage events

`personnel.casualties` authors `significant_damage`, `heavy_below_permille`, `lethal`, `lethal_damage`, and outcome probabilities `stun_permille`, `minor_permille`, `severe_permille` and `killed_permille`. Their sum cannot exceed 1000; remaining probability means no casualty.

A significant compartment damage-tier crossing or explicitly lethal impact can roll an exposed officer's condition. Rolls use the saved deterministic combat random stream. Repeated deposition in the same tick does not repeatedly roll the same officer. Reserve officers are not exposed by a duty, and personnel deployed elsewhere are excluded. Death additionally requires the location to have been heavily damaged before the impact. Death clears the duty permanently and does not spawn a replacement. Ordinary repair does not resurrect officers.

## Away care and persistence

Away team slots may filter `team_type` and `capability`. Repair work counts repair-capable teams only. A deployed medical team follows the target's compartment links and executes its authored patient-care order. Actual patient recovery or stabilisation queues an authored `medical` conversation line through normal message delivery. Mission projections do not expose hidden remote patient state.

**Rescue under threat** includes an optional medical slot, the rescue watch's clinical team and officer, and injured personnel aboard the stranded ship. Omitting Medical leaves the existing launch and repair objectives available. Bringing it restores minor casualties and stabilises severe ones. The party still needs physical recall.

Saves preserve officer condition, stun expiry, treatment and recovery work, facility occupancy, escorts, team order trees, routes and away clinical continuation. Legacy officers default to healthy. Legacy repair-team state is normalised on load. Away save schema 7 adds boarding and physical craft state; schema 5 retains clinical activity and work-completion state and migrates schemas 1–4. Schema-4 clinical records restart only their order-tree continuation because they lack retained activity; patient treatment, physical routes, reservations and mission progress remain intact. This one-time migration can restart an order’s tree-local waiting period; the enclosing World remains schema 13. Content identities still apply, so changing a pack does not make an old content-inclusive save refer to the new pack. Contradictory condition, route, capacity or tree state is rejected.

## Verification

`system.medical` loads synthetic fixtures through production content and command paths. It exercises custom orders, ownership and stale commands, leaderless triage, field stabilisation, facility recovery, projectile casualties, malformed state, permitted network projections and interrupted/resumed work. Regression cases cover retained entry/running actions onboard and away, mixed-state triage, saved-order filters, System-bound casualties without expertise effects and schema-4 clinical migration. `--trace` produces a native/WASM continuation comparison. Existing personnel, away and combat cases remain part of the gate. Production pack verification checks authored references without fixing treatment tuning.

Initial implementation verification on 21 October 2026 (`a28218c`): all 30 Linux native CTest cases, the native build check and production base-pack verification passed. WASM Medical, personnel, away, combat and Controller cases passed; Medical, away, combat and Controller continuation traces matched native. The browser client built successfully. The explicitly invoked two-console graphical rescue journey passed medical dispatch, the injured-officer indicator, optional clinical deployment, Comms, recall, resumed work and completion. It uses a private fixture copy to seed an onboard injury and does not change the shipped starting roster.

Consolidation verification on 21 October 2026: all 30 native CTest cases, native build check and production base-pack verification passed. WASM Medical, personnel, away, combat and Controller cases passed; Medical, away, combat and Controller continuation traces matched native. These cases include the execution, eligibility, duty-binding and save-migration regressions described above. Formatting checks passed. The graphical journey was not rerun for this simulation-only follow-up.

Graphical journeys and actual screen-reader, touch/gamepad and multi-display acceptance remain separate evidence. Automated semantics or compilation do not establish physical-device usability.
