# Duty officers

Duty officers are named members of a ship's watch. They support human and AI operators equally. Unstaffed duties keep their baseline capability. Open **Personnel** from a Console toolbar or Console settings; the roster contains only that Station's departments.

## Assignment

Each department has one owning Station. Each Controller and physical repair team has a duty with one officer slot. Officers belong to a fixed department and may hold one duty at a time, or remain in reserve. Assigning to an occupied duty relieves its occupant; Swap duties exchanges both assignments. There is no Captain override, department transfer or ship transfer.

Reassignment removes the previous contribution immediately. Officers walk the ship's compartment connections, finishing an existing segment before changing direction. Bonuses begin on arrival. Officers attached to teams accompany them; an officer approaching a moving team follows its current route endpoint. Controllers have compartment locations but are not damageable equipment. Ships without combat interiors author a small personnel-only compartment graph. Reserve officers remain at their physical location.

Commands carry the observed roster revision. The host rechecks the Station, department and reservation at execution; stale or conflicting changes are refused. Saves preserve physical location, route progress, reservation and presence. An unmodified legacy hull without personnel has an empty roster and baseline statistics.

## Authoring

The entity's `personnel` object contains:

- `departments`: `id`, localised `label`, owning `station`.
- `traits`: `id`, localised `label`, and `bonuses`, mapping supported slot names to signed permille contributions.
- `duties`: unique `id`, localised `label`, `department`, `compartment`, and either a `controller` plus `console`, or a `team` System ID. A unique Controller ID may omit `console`; ambiguous IDs cannot. `effects` maps fitted System IDs to supported slot arrays.
- `officers`: unique `id`, `name`, `department`, portrait asset reference, trait IDs, starting `compartment` and optional starting `duty`.
- For hulls without combat interiors: `compartments` contains IDs; `connections` contains `from`, `to` and positive `ticks`. Combat hulls use their existing compartment graph.

Every Controller and repair team needs a duty when personnel is enabled. Starting assignments must be unique, in-department and already at the duty location. Effects bind explicitly to compatible systems; unsupported slots, missing portraits, ambiguous Controllers and disconnected interiors fail verification. Portraits are PNG assets up to 256 KiB, shared using opaque content addresses. Workshop exposes these records under Personnel and departments, through its ordinary draft/Undo/Verify/Test flow.

Supported slots are `propulsion` (engine acceleration), `sensors` (radar range), `cycling` (weapon firing rate), `traverse` (turret speed), `reload` (magazine replenishment rate), `recharge` (shield recharge rate), `repair` (team work rate) and `comms` (radio range). A duty can accept an officer whose traits do not apply; the panel shows No applicable bonus. Existing resource grants, faults, ammunition and disabled-system checks still govern physical work.

## Composition and identity

For each affected System and slot, sum applicable contributions from present officers. Each officer/trait contributes once through its assigned duty. Let `b` be that sum in permille:

- Nonnegative: multiplier `(1000 + b) / 1000`.
- Negative: multiplier `1000 / (1000 - b)`.

Thus two +250 contributions produce 1.5x; -1000 produces 0.5x. This preserves V2's additive-positive/reciprocal-negative semantics without exponential stacking. Rate modifiers invert duration calculations, with a minimum one-tick cycle. Integer arithmetic preserves native/WASM continuation; no capped or diminishing positive curve is introduced.

AI Controller messages snapshot their present officer's name and portrait when sent. Old messages retain their original speaker after reassignment. Unstaffed Controllers use their duty identity and the generic crew portrait. Human player profiles remain separate, as do external Comms characters.

## Validation

Synthetic personnel integration exercises real content loading, Station permission boundaries, assignment revisions, swaps, travel, modifier composition and save/restore. Production pack verification checks the starting rosters without fixing balance values. Native/WASM checks and manually triggered UI smoke evidence are recorded separately; builds alone do not establish screen-reader or physical multi-display acceptance.

Verified 21 October 2026:

- `scripts/check.ps1 -Target native`: native integration checks and all eight real-network journeys passed. The final native integration rerun passed 25/25 tests, including the regression that Console claims must not advance officer travel.
- `scripts/check.ps1 -Target web`: WebAssembly integration checks and native/WASM continuation comparisons passed.
- Native `--personnel-smoke --console --local-only` with `base.free-flight`, `base.player`, `base.courier` and seat `main`: opened Personnel, selected an officer, relieved and reassigned them. The rendered panel was inspected at 1280x720 after correcting its content width. The reproducible capture is `build/personnel-smoke.png`.

Physical screen-reader interaction, touch/gamepad operation and six-display performance have not been established by this increment.

The original increment excluded injuries and deaths; [Teams and Medical](medical-teams.md) now adds conditions, treatment and permanent casualties. [Away missions](away-missions.md) adds physical deployment. Recruitment and trait-gated dialogue remain outside these increments.
