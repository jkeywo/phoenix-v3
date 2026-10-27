# Ability authoring

26 October 2026. This reference describes the shared ability schema used by the [ability migration](ability-migration-plan.md). Implementation verification, review findings and remaining acceptance are recorded separately in [the migration evidence](ability-migration-evidence.md).

Abilities, native psychic profiles and temporary grants are definitions in the existing `effects` catalogue. Pack inheritance, explicit overrides, final reference resolution, Workshop editing and dependency export follow the ordinary [content rules](content-format.md). Runtime code uses compiled definitions; authored JSON is not interpreted each tick.

## An ability and its effects

An ability controls when work starts, what it requires and what it costs. Its referenced effects perform the consequences. An officer/equipment grant, the performing source, required apparatus and operating Controller are separate bindings.

This example assumes the referenced operations effects are declared in the same catalogue:

```json
{
  "id": "demo.emergency-shield",
  "kind": "ability",
  "label": "Emergency shield",
  "target": "self",
  "preparation_ticks": 40,
  "preparation_energy_per_second": 100,
  "interruption": "cancel",
  "interruption_recovery_ticks": 0,
  "recovery_ticks": 100,
  "recovery_on": "activation",
  "start_effect": "demo.pay-shield-entry",
  "activation_effect": "demo.apply-emergency-shield",
  "exclusions": [{ "id": "emergency-work", "scope": "apparatus" }]
}
```

`demo.pay-shield-entry` and `demo.apply-emergency-shield` are `kind: "operations"` effects using the existing effect-group contract. For example, the activation group can apply a named shield template. A resulting shield follows its own effect lifetime; the ability's recovery does not rewind or expire that shield. Independent consequences are not automatically removed when their originating ability stops.

| Ability field                                        | Meaning and default                                                                                                                                                                 |
| ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `id`, `kind`, `label`                                | Stable identifier, `"ability"`, and localisable nonempty label of at most 128 characters.                                                                                           |
| `sustained`                                          | `false` by default. An instant/prepared ability completes after activation; a sustained ability retains active work while requested and qualified.                                  |
| `target`                                             | `"self"` by default; `"contact"` or `"ally"` are also supported. Ally includes self. Contact targeting uses the controlling Console's permitted physical contacts.                  |
| `preparation_ticks`                                  | Required preparation work, default zero. At full funding each simulation tick supplies 1,000 milliticks of work.                                                                    |
| `require_healthy`                                    | `true` by default for an officer source. Native duty/choir requirements still apply independently.                                                                                  |
| `energy_per_second`                                  | Ongoing native electrical demand while active, default zero. Requires `sustained: true` and a native power-store binding.                                                           |
| `preparation_energy_per_second`                      | Electrical demand during preparation, default zero. A positive value requires positive preparation work and a native power-store binding.                                           |
| `start_effect`                                       | Optional operations-effect reference, committed when starting preparation/work.                                                                                                     |
| `activation_effect`                                  | Optional operations-effect reference, committed at activation after requirements are rechecked.                                                                                     |
| `interruption`                                       | `"cancel"` by default, or `"pause"`. Defines preparation response to an interrupting apparatus/dependency condition. Explicit cancellation and target changes end that preparation. |
| `shortage`                                           | `"pause"` by default, or `"proportional"`/`"stop"`. Defines ongoing/preparation response to inadequate native funding.                                                              |
| `recovery_ticks`                                     | Source recovery duration, default zero. Instant abilities enter recovery when activated.                                                                                            |
| `recovery_on`                                        | `"activation"` by default for instant abilities, `"stop"` for sustained abilities; both values are supported for sustained work.                                                    |
| `interruption_recovery_ticks`                        | Source-only recovery when preparation is cancelled, default zero. Cancelling preparation does not otherwise apply activation recovery.                                              |
| `apparatus_recovery_key`, `apparatus_recovery_ticks` | Optional separate shared apparatus recovery. A positive duration requires a valid explicit key. Abilities using the same apparatus and key share that readiness deadline.           |
| `exclusions`                                         | Up to 16 `{ "id", "scope" }` entries; scope is `"source"` or `"apparatus"`. Default scope is source.                                                                                |
| `psychic_effect`                                     | Optional shared native psychic-profile reference for a sustained psychic ability.                                                                                                   |

Immediate costs belong in start/activation operations groups; `energy_per_second` is not an implicit instant activation cost. Groups may consume exact resources through shared stores, use entry-energy payment, and apply supported effects. Named entry-energy payers must resolve to native equipment reserves.

With zero preparation, start and activation form one required transaction, submitted as ordered start and activation frames through the shared effect batch executor. Each frame retains all its explicit resource costs, entry energy and operations. Their combined operations must fit the 32-operation group limit, combined entry energy must not exceed 1,000,000,000, and two paid entry groups must name the same payer. Failure commits neither group and does not start recovery. For prepared work, the start step has already completed; a later interruption or failed activation does not refund it. The final activation's required costs/effects remain atomic.

Ability operations use the shared typed context: `owner`, `source` and `instigator` identify the performing officer or equipment on the caster; `subject` identifies the caster ship; `target` identifies the selected recipient, defaulting to self. Explicit costs use the same payer roles. Equipment `$source` and `$owner` bindings resolve the performing equipment; an officer source has officer identity and does not implicitly acquire apparatus equipment. Native effects applied by an ability retain that source context for their own rules.

Existing ability aliases retain their original meaning: `protected` selects the caster and `attacker` selects the recipient. The ability adapter translates them locally to `subject` and `target` in operations and explicit cost payers; global damage-rule roles are unchanged. Supported equipment-role references (`$owner`, `$source`, `$target`, `$hit`) use the same typed context without adding new authoring syntax.

Source recovery is keyed to the performing officer/equipment and ability. Moving an officer, refreshing a grant, removing/regranting a temporary ability or changing control endpoints cannot clear the recovery. A replacement officer has their own readiness. Duplicate lifetime grants of the same source ability do not create extra concurrent cast slots or bypass that recovery.

Exclusions are held through preparation, active operation and pauses. A conflicting start is rejected; it never implicitly cancels the holder or spends the rejected start's costs. Apparatus-scoped exclusions need an explicit apparatus binding, or the source equipment's default apparatus.

## Static grants and control endpoints

An officer's `abilities` array grants referenced ability IDs. A fitted System's `abilities` array grants equipment abilities. Each list supports up to 32 distinct references. Several abilities can operate concurrently unless their declarations conflict. A capability that should remain inaccessible until a temporary grant arrives can have a control endpoint without a static grant.

An entity's `ability_controls` object declares named endpoints:

```json
{
  "ability_controls": {
    "emergency-shield": {
      "source": "equipment",
      "owner": "support-unit",
      "ability": "demo.emergency-shield",
      "controller": "support",
      "console": "engineering",
      "enabled": "activate-emergency-shield"
    }
  }
}
```

The corresponding `support-unit` System grants `"abilities": ["demo.emergency-shield"]` and has a native `power_store` for the example's preparation demand. Its Controller declares the activation request:

```json
{
  "id": "activate-emergency-shield",
  "action": "ability_activate",
  "initial": 0,
  "consumers": []
}
```

`source` is `"equipment"`, `"officer"` or `"duty"`; `owner` names the source equipment, fixed officer or personnel duty respectively. A duty endpoint resolves the currently assigned eligible officer. `ability` selects a fixed shared ability. `apparatus` optionally names equipment supplying native operation/funding; equipment sources use their own equipment when this is omitted. Officer/duty abilities needing apparatus must name it explicitly. Funded apparatus must resolve to the real native resource consumer and its reserve.

`controller` and `console` identify the ordinary command owner. `enabled` names its activation/intent channel. Optional `target` names a zero-initialised nonnegative `intent_integer` channel bounded to physical entity IDs (at most 1,000,000,000); non-self abilities require it. Source department and Console Station ownership are validated. Reusing definitions does not grant another Console control or knowledge.

Fixed instant/prepared abilities use `ability_activate`, a Boolean **Request** action: value one starts and zero cancels preparation. Existing Controller request IDs, revisions and receipts distinguish new requests, retries and completed work. Each activation channel has exactly one declared ability endpoint executor and no ordinary System consumers. An unavailable or failed request is not silently retried as new work. Repeated intentional uses require new request identities.

Sustained abilities use zero-initialised `intent_boolean` **Setting** channels. Requested intent can remain enabled through recovery or an authored pause. Setting a target or enabling intent is not proof that work actually activated or received funding. Native qualification and the physical execution boundary determine output.

Optional `planner` names a same-Console planning Controller. It declares matching enabled/target channel names and ranges. A sustained enabled preference is `intent_boolean`; an instant ability's planning enabled preference is also `intent_boolean`, while its executor is `ability_activate`. Target preferences remain `intent_integer`. Ordinary Controller trees deliberately translate preferences into executor commands. Planning channels do not invoke physical effects themselves.

Preparation captures its target and revalidates it at activation. Changing target ends the old work; paid progress cannot be moved to an unrelated target. Reassignment, grant expiry and endpoint replacement clear obsolete intent and reject stale generation-bound controls. Existing apparent/bearing contact rules remain native; this ability schema does not add arbitrary coordinate or area-selection controls.

## Temporary ability grants

Define a timed grant in the effects catalogue:

```json
{
  "id": "demo.temporary-shield-training",
  "kind": "ability_grant",
  "ability": "demo.emergency-shield",
  "duration_ticks": 400
}
```

Apply it through a normal required operations group, binding either `officer` or `equipment` on the target entity:

```json
{
  "kind": "apply",
  "target": "protected",
  "effect": "demo.temporary-shield-training",
  "grant": "emergency-training",
  "officer": "engineer"
}
```

The `engineer` officer and compatible control endpoint must already exist. Granting an ability does not fabricate a Controller writer or install its apparatus. `equipment` selects a fitted equipment source instead; a grant operation cannot specify both source kinds.

Applying the same parent/local grant identity refreshes its expiry without recreating execution or changing definition. Different grants have independent lifetimes. Removal uses the same `target`, local `grant` and source binding with `kind: "remove"`. Expiry/removal stops that grant's dependent preparation/active work, retires its queued commands and releases its exclusions. Source recovery and independent effects it already produced remain. `all_sources` removal remains the existing periodic-effect operation, not a wildcard ability revocation.

## Psychic profiles and apparatus

Shared native psychic profiles contain physical parameters, while shared abilities contain costs and lifecycle policy:

```json
{
  "id": "demo.veil-profile",
  "kind": "psychic",
  "psychic_kind": "cloak",
  "strength_permille": 700
}
```

```json
{
  "id": "demo.veil",
  "kind": "ability",
  "label": "Veil",
  "sustained": true,
  "target": "self",
  "shortage": "proportional",
  "energy_per_second": 100,
  "recovery_ticks": 100,
  "recovery_on": "stop",
  "psychic_effect": "demo.veil-profile"
}
```

`psychic_kind` is `"cloak"`, `"scramble"` or `"ward"`. Their ability target policies are self, contact and ally respectively. Native psychic abilities require sustained operation, proportional funding and compatible psychic apparatus. Profiles support optional `label`, `strength_permille`, and the existing ghost fields `ghost_count`, `apparent_class`, `offset_mm`, `spacing_mm`, `speed_mm_s`, `acceleration_mm_s2`, `turn_mdeg_s`, `radius_mm`, `scan_ticks` and `fade_ticks`. They do not define their own energy or recovery.

A psychic System uses `supported_abilities` for its compatibility list. Its separate `abilities` field, if authored, is an actual grant to the equipment itself. Compatibility alone never gives an unstaffed choir the officers' powers.

Choir `seats` keep `duty`, `console`, `controller`, `enabled`, `target` and optional fixed `ability`. Each compiles into the standard endpoint ID `choir.<apparatus>.<duty>`. Without a fixed selection, the current officer must have exactly one compatible available ability. Authors needing several independently usable abilities should declare separate fixed endpoints with separate channels. An ambiguous dynamic selection is unavailable; it does not enable every matching power. `planning_controller` retains the established choir preference binding.

The native psychic contribution keeps ghost trajectories, reception and concealment/ward state. Shared ability execution owns activation, preparation, demand, recovery and grant lifetime. Funding interruptions do not allocate fresh ghost activation identities. Observer-local rejection, fading, retained evidence, strongest-source selection and ordinary sensing/publication remain as described in [Psychic choir](psychic-choir.md).

The retired fields are entity-local `psychic_powers`, officer `power` and choir `powers`. Maintained content references shared templates instead; there is no permanent legacy execution path.

## Controller ability observations

A Controller tree declares the endpoints it needs:

```json
{ "ability_bindings": ["emergency-shield"] }
```

This adds compiled observations with stable names:

| Observation                            | Type and value                                                           |
| -------------------------------------- | ------------------------------------------------------------------------ |
| `ability.emergency-shield.phase`       | Text phase: unavailable, ready, preparing, active, paused or recovering. |
| `ability.emergency-shield.ready`       | Boolean actual readiness from the standard ability status.               |
| `ability.emergency-shield.preparation` | Integer completed preparation work in milliticks.                        |
| `ability.emergency-shield.recovery`    | Integer ticks remaining on applicable source/apparatus recovery.         |
| `ability.emergency-shield.output`      | Integer current funded/native output in permille.                        |
| `ability.emergency-shield.blocked`     | Boolean readiness/operation blocker.                                     |
| `ability.emergency-shield.requested`   | Boolean requested intent or accepted ongoing activation.                 |

Only endpoints executed or planned by that Controller on its Console can be bound. Facts come from the frozen permitted decision snapshot, using the same status calculation as human presentation; Controller AI does not build the full GUI board or inspect unrestricted world state. `command.<channel>` and request receipt observations retain their existing meanings and do not replace ability readiness/progress.

For an intrinsic tree write binding, `"action": "ability_activate"` supplies request semantics. When declaring only a channel name/type, include `"semantics": "request"` explicitly. A mismatched setting/request contract fails validation.

## Tick phases and stable execution

Controller decisions read frozen permitted facts captured before physical work. Admitted channel writes are applied before the normal ability-preparation phase; a later physical consequence cannot become an earlier observation for another Controller in that decision pass.

Ability preparation processes ships by stable entity ID and existing grants by stable grant ID. It resolves expiry, cancellation, current source/target requirements and exclusions before committing a new start. Competing starts therefore have a deterministic winner; definitions do not choose a global reaction priority. A grant created by a later effect does not recursively start work inside that effect transaction.

Ongoing ability demands join the apparatus's ordinary resource allocation before that allocation commits. Funded equipment work then advances preparation in milliticks. When the required work is reached, activation revalidates its requirements and commits its required effects once at that boundary. Maintained psychic contributions are synchronised from the resulting ability state; native projection movement/reception and sensing retain their scheduled phases. Secondary payload and shield reactions use their existing deferred execution phases.

Restore reconstructs derived funding, output and reception winners from the saved authoritative grants and deadlines. It does not advance preparation, pay again or issue another activation. The immutable ability/native-profile catalogue is compiled for the selected content and shared by its fitted adapters; it owns no mutable execution state.

## Supported bounds and continuation

Ability preparation, source recovery, interruption recovery, apparatus recovery and temporary-grant duration are bounded to 1,000,000 simulation ticks. Temporary duration must be positive. Energy rates are nonnegative integers bounded to 1,000,000,000. Source ability lists contain at most 32 distinct references, the catalogue at most 1,024 ability templates, and an entity at most 256 combined generic/choir endpoints. A tree can bind at most 256 distinct endpoint observations.

Native profiles retain bounded geometry and motion: strength 1–1,000 permille, 1–16 ghosts, fade 1–200 ticks and scan duration 1–36,000 ticks. Full validation of physical profile fields occurs through the native profile compiler. These are representational/support limits, not production balance assertions.

Runtime supports at most 4,096 live grants per entity and 32,768 recovery ledger entries, with bounded generation/request identities. The combined static officer/equipment grant count is checked against that same grant limit during compilation. Recovery entries remain while needed for source readiness; granting/removing an ability cannot erase the deadline. Saves retain source/grant identities, execution/preparation, request outcomes, recovery and native psychic continuation. Restoring rebuilds derived funding and reception winners without paying, activating or advancing work again. See [migration evidence](ability-migration-evidence.md) for the delivered save-schema boundary and actual verification results.

## Requirements, completed work and observed properties

An ability may declare `requirements` as a shared typed Boolean expression. It is evaluated at admission, preparation completion and each native operating boundary. Missing or invalid required facts fail the requirement. `requirement_failure: "end_retain_intent"` ends the execution normally, releases exclusions, applies its native recovery policy, and preserves sustained requested intent. A later eligible start receives a fresh activation identity and must pass ordinary costs, readiness and targeting again.

`family` is an optional stable identifier grouping completed native work for authored rules. The `owner.completed_work` fact, optionally filtered with `family`, reads the sum of frozen actual output permille for that source in the current work phase. It includes funding and fault reductions, excludes preparation and zero-output work, and remains available after the running grant ends. Before the current native work phase completes the fact is missing; after that phase, a source with no completed work reports zero. This distinction prevents earlier source preparation from treating unfinished work as an idle recovery step. It does not infer output from enabled intent. Multiple simultaneous abilities contribute independently. Saving and restoring preserves the completed work boundary and activation receipts.

`observations` is an ordered array of custom property identifiers. Existing ability Console permissions govern these effective-only rows, shared with authorised AI facts. Labels and enum choice labels are localised. Undeclared properties remain absent; base values, contribution provenance and hidden captured facts are never implicitly exposed.

Explicit officer rule bindings can declare `eligibility: "lifetime"` for rules that follow a living officer through idle assignments and travel. The default duty eligibility remains unchanged for buffs and equipment participation. Production psychic strain uses lifetime bindings; no native fatigue loop is added.

Controller trees can opt into typed observed values with `ability_property_bindings: [{"endpoint":"choir.ward","property":"base.psychic.strain"}]`. This adds `ability.choir.ward.property.base.psychic.strain` to the tree registry, with the property's numeric, Boolean or text type. The endpoint must belong to that execution/planning Controller on its Console. Reads come from the same frozen permitted ability observations supplied to model Controllers; an unobserved or undeclared property stays missing. No tree property binding reads live private owner state.

A fixed sustained endpoint can explicitly bind its enabled channel to `ability_activate` request semantics, using start and cancel requests. Its default remains the ordinary Boolean setting; dynamic choir selection retains Boolean settings. Accepted sustained requests interrupted by a failed requirement retain intent through recovery, including save/restore, and re-enter with a fresh activation. Failed admission does not queue a rejected request, and explicit cancellation clears retained intent.
