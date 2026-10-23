# Duty officers

Duty officers are named members of a ship's watch. They support human and AI operators equally. Unstaffed duties keep their baseline capability; psychic choir seats require a compatible specialist to supply a power. Open **Personnel** from a Console toolbar or Console settings; the roster contains only that Station's departments.

## Assignment

Each department has one owning Station. Each Controller and physical team has a duty with one officer slot. [Psychic choirs](psychic-choir.md) additionally author multiple seat duties; each seat accepts one compatible psychic officer. Officers belong to a fixed department and may hold one duty at a time, or remain in reserve. Assigning to an occupied duty relieves its occupant; Swap duties exchanges both assignments. There is no Captain override, department transfer or ship transfer.

Reassignment removes the previous contribution immediately. Officers walk the ship's compartment connections, finishing an existing segment before changing direction. Maintained property contributions begin on arrival. Officers attached to teams accompany them; an officer approaching a moving team follows its current route endpoint. Controllers have compartment locations but are not damageable equipment. Ships without combat interiors author a small personnel-only compartment graph. Reserve officers remain at their physical location.

Commands carry the observed roster revision. The host rechecks the Station, department and reservation at execution; stale or conflicting changes are refused. Saves preserve physical location, route progress, reservation and presence. An unmodified legacy hull without personnel has an empty roster and baseline statistics.

## Authoring

The entity's `personnel` object contains:

- `departments`: `id`, localised `label`, owning `station`.
- `traits`: `id`, localised `label`, and `effects`, an array of shared modifier-effect IDs. Qualification and psychic traits may have an empty array. Each trait supports at most 64 references and 256 compiled named contributions. Referenced officer effects must be indefinite (`duration_ticks: 0` or omitted).
- `duties`: unique `id`, localised `label`, `department`, `compartment`, and either a `controller` plus `console`, or a `team` System ID. A unique Controller ID may omit `console`; ambiguous IDs cannot. `bindings` maps fitted System IDs to arrays of target-role names from the referenced effects. Psychic duties instead name their `choir`; qualification identities remain ordinary trait IDs.
- `officers`: unique `id`, `name`, `department`, portrait asset reference, trait IDs, starting `compartment` and optional starting `duty`.
- For hulls without combat interiors: `compartments` contains IDs; `connections` contains `from`, `to` and positive `ticks`. Combat hulls use their existing compartment graph.

Every Controller and physical team needs a duty when personnel is enabled. Starting assignments must be unique, in-department and already at the duty location. Roles bind explicitly to compatible systems. Unknown properties, duplicate role bindings, incompatible team capabilities, missing portraits, ambiguous Controllers and disconnected interiors fail verification. An effect may have no matching roles at a duty; if any required role matches, all its roles must bind. A duty supports at most 256 distinct System/role/property rows. Portraits are PNG assets up to 256 KiB, shared using opaque content addresses. Workshop exposes these records under Personnel and departments, through its ordinary draft/Undo/Verify/Test flow.

The native property catalogue covers `propulsion`, `speed`, `turn`, `sensors`, `cycling`, `traverse`, `reload`, `recharge`, `off_recharge`, `sustain`, `discharge`, `protection`, `repair`, `medical`, `security` and `comms`. The [content format](content-format.md#modifier-effect-catalogue) documents their compatible equipment and a reusable trait example. A duty can accept an officer whose traits do not apply; the panel shows No applicable bonus. Existing resource grants, ammunition and hard availability checks still govern physical work.

## Composition and identity

Officer effects use the same property calculation as temporary scenario effects and applicable native fault/boost adapters. Add signed permille deltas, then apply them once to the unmodified base:

```text
effective = base × max(0, 1000 + sum(delta_permille)) / 1000
```

Two +250 contributions produce 1.5×. A total of -1000 produces zero; positive contributions can offset an output penalty but cannot enable destroyed, disabled or unfunded machinery. This replaces the original officer-only reciprocal-negative calculation. Integer arithmetic checks aggregate bounds and overflow. Native consumers retain their physical bounds, fractional progress and committed cooldowns. A positive firing rate has a minimum one-tick interval; a zero rate blocks new work.

Each contribution retains officer, trait, effect and named-contribution identity. Repeated observations or duty reconciliation do not accumulate copies. Relief, reassignment, travel, health and detention change actual eligibility. Team officers follow their physical teams; deployed specialists contribute to their actual work context and do not retain a second shipboard copy. Removing a contribution changes future work without resetting reserves, erasing completed work or charging previously paid work again.

Personnel displays compiled trait contributions and applicable duty property multipliers. A mechanism's normal permitted feed can show an effective value without disclosing another Station's officer or a hidden scenario source. Psychic power identities, choir eligibility and funding remain under their existing native rules.

AI Controller messages snapshot their present officer's name and portrait when sent. Old messages retain their original speaker after reassignment. Unstaffed Controllers use their duty identity and the generic crew portrait. Human player profiles remain separate, as do external Comms characters.

## Validation

Synthetic personnel integration exercises real content loading, Station permission boundaries, assignment revisions, swaps, travel, modifier composition and save/restore. Production pack verification checks the starting rosters without fixing balance values. Native/WASM checks and manually triggered UI smoke evidence are recorded separately; builds alone do not establish screen-reader or physical multi-display acceptance.

The following evidence belongs to the original Personnel increment, verified 21 October 2026. It does not establish validation of the later [property-effect migration](property-effects-migration-plan.md):

- `scripts/check.ps1 -Target native`: native integration checks and all eight real-network journeys passed. The final native integration rerun passed 25/25 tests, including the regression that Console claims must not advance officer travel.
- `scripts/check.ps1 -Target web`: WebAssembly integration checks and native/WASM continuation comparisons passed.
- Native `--personnel-smoke --console --local-only` with `base.free-flight`, `base.player`, `base.courier` and seat `main`: opened Personnel, selected an officer, relieved and reassigned them. The rendered panel was inspected at 1280x720 after correcting its content width. The reproducible capture is `build/personnel-smoke.png`.

Physical screen-reader interaction, touch/gamepad operation and six-display performance have not been established by this increment.

The original increment excluded injuries and deaths; [Teams and Medical](medical-teams.md) now adds conditions, treatment and permanent casualties. [Away missions](away-missions.md) adds physical deployment. Recruitment and trait-gated dialogue remain outside these increments.
