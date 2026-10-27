# Psychic choir

The current ship fitting places Phantom choir and Psychic ward on Cruiser Science and Veil on Courier Support. See [station ownership and workloads](cruiser-science-comms-captain.md). Earlier Comms examples below describe the shared authoring machinery, not current station ownership.

The Cruiser's Science Station owns Phantom choir and Psychic ward alongside conventional scans and shields. Veil is fitted to Courier Support. House-trained psychic specialists are separate from Guild navigators.

Officers grant shared abilities; the maintained fittings give each psychic officer one existing power. Several officers can serve a choir concurrently. Choir seats are personnel duties, with one officer per seat; assignment, travel, department ownership, injury and treatment use the existing Personnel and Medical rules. Removing an officer clears that seat's execution and planning intent. Recovery follows the officer rather than being inherited by their replacement. Other duties remain functional without named officers; the maintained psychic fittings require their compatible healthy, present specialists.

The [shared ability authoring reference](ability-content.md) describes reusable definitions, preparation, grants and control bindings. Current migration verification is recorded in [the ability migration evidence](ability-migration-evidence.md); the historical results below describe the earlier implementation.

## Powers

**Cloak** reduces the projecting ship's detection distance. Its effective concealment is its funded strength multiplied by the remaining low-emission fraction. Actual generation, propulsion, active investigation, transmitted messages and recent weapon fire contribute to exposure. Weapon exposure persists for one simulation second. Firing does not switch cloak intent off. Existing observations remain dated, and lost real contacts retain predictions from their last observed motion with growing uncertainty.

**Scrambling** projects an authored group of apparent ships against a selected known victim. Apparatus range and a directional footprint aimed at the last permitted target position determine reception. Other receivers in that footprint can be affected, including allies. Each apparent ship follows authored speed, acceleration and turning constraints. A receiver also needs actual sensor coverage of its apparent position. Ghosts do not alter real ships' positions or details, nor reduce sensor range.

Ghosts enter the same permitted contact picture used by players and AI. They can be inspected, scanned and targeted. Weapons spend their normal ammunition and energy, and their trajectories continue through the apparent hull. A detailed scan or damaging weapon intersection dispels a ghost for that receiving ship. It fades instead of exploding. A rejection receipt prevents its immediate recreation within the same activation. Ward suppression and lost reception preserve evidence without establishing that a contact was false.

**Ward** protects one ship, self or a known ally, while apparatus coverage and funded work remain available. It suppresses ghost reception when its effective strength meets the incoming strength. It does not identify the attacker or erase old observations. Overlapping wards or scramblers use the strongest applicable contribution, with stable ordering for ties.

Apparatus failure, partial funding, structural faults and officer availability affect actual work. The choir requests the sum of its active powers' energy demands; its funded fraction applies proportionally to all seats. Thermal behavior comes from its real resource store and cooling links. Each psychic officer owns shared strain across their psychic abilities. Strain follows the officer through reassignment and travel. Actual funded output adds strain, while doing no psychic work recovers it automatically; injuries and Medical facilities are not part of this mechanic.

## Authoring and interfaces

Shared `effects` catalogue definitions separate `kind: "ability"` lifecycle/cost metadata from referenced `kind: "psychic"` native profiles. The profile's `psychic_kind` is `cloak`, `scramble` or `ward`, with `strength_permille` and the existing bounded ghost geometry, motion, acquisition and fade fields. The ability supplies the localisable label, sustained target policy, `energy_per_second`, recovery and `psychic_effect` reference. Native psychic abilities use proportional funding. Ordinary pack overrides can change a shared ability or profile for all its consumers.

A `kind: "psychic"` System declares `compartment`, `power_store`, `range_mm`, `arc_mdeg`, compatible ability IDs in `supported_abilities`, and `seats`. Each seat binds a `duty`, `console`, `controller`, `enabled` channel and `target` channel, with an optional fixed `ability`. The channels remain zero-initialised `intent_boolean` and bounded nonnegative `intent_integer` settings for these sustained abilities. Each seat compiles into a shared ability endpoint, `choir.<apparatus>.<duty>`. Without a fixed ability, the current officer must have one unambiguous compatible grant. The same Controller executor admits human and AI intent. The apparatus itself uses an ordinary `system_enabled` channel. A System's separate `abilities` list grants abilities to the equipment; its compatibility list does not.

A personnel duty binds `choir` instead of `controller` or `team`. An officer's `abilities` list references shared ability templates; starting choir assignments must be compatible. The apparatus and its seat duties occupy the same compartment. The owning department and Console must belong to the same Station. Every Controller still has its ordinary separate duty. Generic fixed ability endpoints can expose several abilities independently where authored, using explicit channels and exclusions. Entity-local `psychic_powers`, officer `power` and choir `powers` are retired authoring fields.

An optional `planning_controller` names a same-Console Controller with matching seat channels. Authored ordinary Controller trees can read these preferences and write execution intent. The Cruiser's Simple and Middle profiles expose planning while delegating execution; Detailed exposes execution directly. Automatic preserves ordinary authored intent. Target commands require known physical ships; ward targets must be self or observed allies.

Workshop exposes shared ability/native-profile templates, apparatus, seats, officer ability lists and generic control endpoints through its normal draft/Undo/Verify/Test flow. Missing references, incompatible grants, invalid geometry, foreign ownership and conflicting channel bindings fail verification. Controller trees can declare `ability_bindings` for standard phase, readiness, preparation, recovery, output, blocked and requested facts, scoped to their own execution/planning endpoints. Human and AI status uses the same permitted native facts.

Apparent contacts receive stable identities from the ordinary entity identity allocator without creating physical bodies. Their visible IDs do not classify them as ghosts. Weapon and scanner targets resolve apparent evidence without requiring a physical entity. Ghost provenance stays authoritative; permitted Console observations carry apparent evidence and only reveal a dispel after it happens. Network projections explicitly allowlist choir status, fading contacts and retained tracks. Console readouts and actions use the same model for graphical and semantic interaction.

## Shared simulation paths

Physical and apparent contacts use the same sensor admission, capability acquisition, retained tracks, findings publication and permitted evidence. A sensor that filters out ships cannot observe ghost ships; a sensor without identity capability cannot identify them. Detailed-scan exposure belongs to each scanner, so switching scanners does not combine progress. Unsupported physical details remain unavailable. Rejection receipts remain observer-local psychic state.

Shared ability execution owns activation, recovery and funding demands. Native psychic contributions own projection shapes, concealment and ward/reception behaviour; they link to the shared grant and activation rather than copying a seat-owned ability lifecycle. Each tick computes the strongest ward and scrambler in stable contribution order; derived winners are rebuilt after restore. Preparation and control reuse permitted contact lookups. Command admission and physical commit validate the relevant requirements without publishing hidden target facts.

Personnel changes clear bound execution and planning settings through the Controller's setting-reset helper, including pending writes for those channels. Other seats and operator generations remain intact. Apparent trajectories use ordinary `Pose`/`Motion`, basic steering and acceleration, and the same fixed-step position integrator as physical ships.

## Persistence and current verification

Shared ability saves retain direct/temporary grants, source-owned recovery, preparation, requested intent and request outcomes. Native psychic saves retain contribution links, activation identities, projection trajectories, observer identities, rejection receipts and weapon emission deadlines. Funded strength and reception winners are rebuilt from restored native grants without advancing time, recreating activations or paying again. See [the ability migration evidence](ability-migration-evidence.md) for the delivered schema and executed verification; the records below are historical.

## Historical persistence and verification — 22 October 2026

At this historical checkpoint, World saves optionally contained `psychic`: apparatus seat continuation, projection trajectories, observer contact identities, rejection receipts, recovery deadlines and weapon emission deadlines. Psychic continuation version 1 used World schema 17. Those earlier versions are not the current shared-ability continuation contract. Per-scanner exposure and required duration belonged to ordinary sensor samples; apparent tracks retained an internal source marker omitted from permitted projections.

`system.psychic` uses independent synthetic fittings through production loaders, command admission and authoritative execution. It covers concurrent powers, shortages, injury, reassignment and queued orders, sensor isolation, friendly reception, duplicate powers, wards, scan and weapon dispels, fading, hostile save input, network projection filtering and exact continuation. Cross-platform traces exclude fixture ZIP timestamps; within-run restore comparisons retain full content identities. The review-refactor cases additionally cover missing sensor identity capability, ship filtering, independent scanner exposure and resume, deliberate finding publication to an unfed Console, equal-strength overlapping fitted choirs, selective planning/execution resets, old psychic-save rejection, fractional movement and world boundaries. Winner selection is computed once per receiver from a stable choir query; target enumeration reuses its existing observation instead of recapturing contacts per candidate.

Verification after the review refactor on 22 October 2026:

- Native: all 33 runnable CTest cases passed, including `system.psychic`. The separate inference adapter case requires a local HTTP socket and was blocked by the sandbox.
- WASM: all 30 Node cases passed. All 11 native/WASM continuation comparisons matched, including psychic, sensors, combat and Controller runtime. Repeated native psychic traces also matched.
- Production pack verification, native `--build-check` and source formatting passed. WASM launch preflight and launcher tests passed in the original choir implementation run; they were not rerun for this refactor.
- In the original choir implementation run, the explicit `phoenix_psychic_smoke` target compiled. Its graphical journey could not run because Xvfb could not bind a sandbox socket. No graphical or screen-reader pass is claimed. The network service suite was likewise blocked by socket restrictions.

Native/WASM checks do not establish actual screen-reader, physical-device or six-display acceptance.

## Shared psychic strain

Psychic strain is authored using custom properties and ordinary shared rules. The native choir still owns energy allocation, fault-adjusted output, cloaking, wards and ghost contacts. Every completed native work phase freezes the actual output and activation identity before later interruption can retire the grant. The `psychic` ability family selects the output counted by the officer's rules; concurrent abilities add their actual output.

The initial content starts officers at zero strain, with capacity 60,000 and gain 2,000 per second at full output. At capacity, the officer enters recovery and all their psychic powers end at the next native operating boundary, preserving sustained requested intent. Native cooldowns and ghost retirement still apply. Idle recovery is 4,500 per second, with reactivation permitted at 25% capacity: roughly 30 seconds fresh work, ten seconds rest, then 22.5 seconds of further work. Fully resting restores the full fresh interval. Funding outages, off-duty time and travel also recover strain when no psychic work completes.

Recovery is a base enum latch. Temporary state overrides affect ability eligibility without overwriting this base: rules use explicit base reads to maintain and clear the latch. Capacity and rates are ordinary modifiable attributes; accumulated strain is a conserved quantity changed only by explicit operations. Property definitions, thresholds and rules can be overridden by mods. These are initial tuning choices, not frozen performance acceptance assertions.

Only the ability's authorised Console and permitted AI observations receive its authored strain, capacity, safe-resume threshold and effective recovery state. Enemy crews and unrelated departments receive no new private officer facts.
