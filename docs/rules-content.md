# Shared rule authoring

This reference describes the compiled authoring format used by the [rules migration](rules-migration-plan.md). The migration is in progress; this reference does not establish runtime delivery or acceptance. Rules extend native mechanisms, including damage resolution, stores, property modifiers and region geometry.

## Templates and bindings

Rules are ordinary entries in the mod-extensible `effects` catalogue. A rule references an `operations` effect and adds a trigger, condition, costs and optional computed values. Catalogue inheritance and overrides work as for other effect templates.

```json
{
  "test.retaliation": {
    "kind": "rule",
    "trigger": "post_compartment_damage",
    "when": {
      "op": "gt",
      "args": [
        { "fact": "event.inflicted" },
        { "value": 0, "unit": "damage" }
      ]
    },
    "effect": "test.retaliation_operations"
  },
  "test.retaliation_operations": {
    "kind": "operations",
    "operations": [
      { "kind": "damage", "target": "attacker", "damage": 5 }
    ]
  }
}
```

A native source binds templates using entries of this form:

```json
[
  {
    "id": "retaliate",
    "rule": "test.retaliation",
    "target": "subject",
    "tags": ["ship"]
  }
]
```

The source determines which triggers it supports and supplies its context. Binding IDs are unique within that source and form part of the execution identity. A binding may select one `equipment` or `officer`; omitting both selects the native event subject. All listed tags must match. A source accepts at most 64 bindings, each with at most 32 distinct tags. Tags follow existing actor tags: nonempty strings of at most 128 bytes.

Bindings live in `entities[*].rules`, `systems[*].rules`, personnel `officers[*].rules` and `traits[*].rules`. Active shield, modifier and ability-grant templates may also own `rules`; their existing native state owns the lifetime. Trait rules must be maintained modifier applications. Their property/role metadata still validates native duty bindings, qualification and equipment capabilities; there is no second executable trait-buff path.

Shield contribution `rules` accept pre/post shield-damage rules in the instant phase. The retired `reactions`, `pre_damage_permille` and `post_damage` contribution fields fail verification. Payload templates use `rules` instead of `bindings`: the rule's post-damage trigger selects shield, compartment, system or entity outcomes. Preserve the old positive-damage filter explicitly with `event.absorbed > 0` for shields and `event.inflicted > 0` for other outcomes. A payload rule may deliberately omit that condition for effect-only contact.

Periodic condition templates contain `duration_ticks` and `rules`. Each binding refers to a periodic `tick` rule; its `period_ticks` sets that rule's cadence and cannot exceed the condition lifetime. The former periodic template `period_ticks` and `effect` fields fail verification. Periodic compartment conditions have no `$hit` equipment and cannot spawn another periodic condition.

## Triggers and phases

`trigger` defaults to `activate`. Supported names are `activate`, `deactivate`, `tick`, `enter`, `exit`, `occupancy`, `maintain`, `pre_shield_damage`, `post_shield_damage`, `pre_compartment_damage`, `post_compartment_damage`, `pre_system_damage`, `post_system_damage`, `post_entity_damage` and `destroy`. Entity damage preserves native aggregate ship, ordnance and destructible hit outcomes; it is distinct from destruction. A binding must use a trigger supported by its native source.

`phase` defaults to `instant`:

- `instant` performs independent consequences once for that execution.
- `periodic` uses `period_ticks`, an integer from 1 to 1,000,000, defaulting to 1.
- `maintained` owns applied contributions while eligible. It requires the `maintain` trigger and allows only `apply` operations. Changes update supported contribution values without recreating their identities. Withdrawal removes owned grants; it does not reverse earlier independent consequences.

Use instant or periodic rules for damage, filling stores, transfers and other repeatable consequences. A maintained rule cannot use those operations.

`damage_permille` is an optional expression on pre-damage rules. It must return an integer in `permille`, bounded from 0 to 1000. Zero cancels the pending damage; 1000 leaves its magnitude unchanged. Native bypass routes, penetration, shield ordering and sharing remain native. A shield hook runs only for a reached contribution. Actual absorption and infliction are available after native resolution.

## Roles and facts

Context roles are `owner`, `source`, `instigator`, `subject`, `target`, `attacker` and `protected`. They distinguish the object owning a binding, the original source, the instigator, the event subject and the selected target. They are bookkeeping and target resolution, not authentication requirements for trusted authored content.

The native adapter freezes facts for the scheduled execution. Projectile adapters capture source-dependent facts at launch and append actual target and hit facts at impact. Ordinary defensive pre-damage rules read their current source; a template's fact spelling does not itself request launch capture.

| Fact                                                                  | Type and unit                   | Availability                             |
| --------------------------------------------------------------------- | ------------------------------- | ---------------------------------------- |
| `tick`, `elapsed`                                                     | Integer, `ticks`                | Native execution clock                   |
| `<role>.entity`                                                       | Entity reference                | Bound role                               |
| `<role>.exists`                                                       | Boolean                         | Bound role                               |
| `<role>.enabled`, `<role>.eligible`                                   | Boolean                         | Native equipment/source eligibility      |
| `<role>.heat`                                                         | Integer, `heat`                 | Native equipment heat                    |
| `<role>.output`                                                       | Integer, `permille`             | Native funded output                     |
| `<role>.resource`                                                     | Integer, `resource:<ID>`        | Explicit `store` and `resource` metadata |
| `<role>.property`                                                     | Integer, native descriptor unit | Explicit `property` metadata             |
| `event.presented`                                                     | Integer, `damage`               | Pre/post damage                          |
| `event.strength`                                                      | Integer, `strength`             | Pre/post shield damage                   |
| `event.modified`, `event.negated`, `event.absorbed`, `event.residual` | Integer, `damage`               | Post shield damage                       |
| `event.inflicted`                                                     | Integer, `damage`               | Post compartment/system/entity damage    |

Examples:

```json
{ "fact": "source.property", "property": "speed" }
```

```json
{ "fact": "target.resource", "store": "magazine", "resource": "test.ammo" }
```

Property facts use the existing supported native property descriptors and their units. They do not expose arbitrary C++ fields or create mutable properties. Equipment-sensitive facts resolve the equipment supplied by the role's native source or binding. `enabled` reads the native control flag; `eligible` also includes native structural, duty and location restrictions. Per-role `output` reads that actor's last completed native resource phase, independently of this rule's own upkeep lease. It is zero for disabled or blocked equipment; eligible actors without a powered equipment operation report full output.

Unavailable event fields, unknown properties and unknown resources fail compilation. A missing runtime role/fact remains missing; use `present` where absence is an intended condition. Missing numeric facts cannot silently become zero.

## Expressions

Expressions are scalar literals, typed integer literals, facts or operator applications. The compiler checks types and units once and produces bounded indexed nodes; runtime evaluation does not interpret JSON.

```json
{
  "op": "clamp",
  "args": [
    { "fact": "target.heat" },
    { "value": 0, "unit": "heat" },
    { "value": 500, "unit": "heat" }
  ]
}
```

Bare Boolean, integer, text and null literals are accepted. Integers with a physical unit use `{ "value": 5, "unit": "damage" }`; a bare integer has no unit. Only integers have units. Text is bounded to 256 bytes. Units include native property descriptor units, `ticks`, `milliticks`, `damage`, `strength`, `energy`, `heat`, `permille` and `resource:<known resource ID>`.

| Operators              | Contract                                                                                      |
| ---------------------- | --------------------------------------------------------------------------------------------- |
| `all`, `any`           | 2–32 Boolean arguments                                                                        |
| `not`                  | One Boolean argument                                                                          |
| `eq`, `ne`             | Two arguments of matching type and unit                                                       |
| `lt`, `le`, `gt`, `ge` | Two integers with matching units                                                              |
| `present`              | One value; returns whether it exists                                                          |
| `add`, `subtract`      | Two integers with matching units                                                              |
| `multiply`             | Two integers; at least one is a unitless scalar                                               |
| `divide`               | Two integers; divisor is scalar, or matching units cancel                                     |
| `ratio`                | Numerator, denominator and scale; numerator/denominator units match, result uses scale's unit |
| `min`, `max`           | 2–32 integers with matching units                                                             |
| `clamp`                | Value, lower bound and upper bound with matching units                                        |

All arithmetic uses checked signed 64-bit integers. Division truncates towards zero. `ratio` checks its multiplication before division; an overflowing intermediate fails even if a mathematical final result would fit. Division by zero, overflow, missing operands or inverted clamp bounds makes the execution uncommittable. Boolean composition does not hide invalid arithmetic in another argument.

Each expression has at most 256 nodes and 16 nested levels. A rule binds at most 256 distinct facts. Unknown fields and operators fail compilation.

## Conditions and computed values

`when` must return Boolean. A false condition performs no work.

An operations template supplies the fixed operation kinds, resources, grants and targets. `values` overrides supported numeric fields by zero-based operation index:

```json
{
  "values": {
    "0": {
      "damage": {
        "op": "min",
        "args": [
          { "fact": "event.inflicted" },
          { "value": 50, "unit": "damage" }
        ]
      }
    }
  }
}
```

- `damage` requires a damage operation, unit `damage`, and a result from 0 to 1,000,000.
- `amount` requires a quantified store operation, the exact `resource:<ID>` unit, and a result from 0 to the native resource limit, 1,000,000,000,000. `fill` has no amount override.
- `delta_permille` requires a modifier `apply`, unit `permille`, and a result from −100,000 to 100,000. It explicitly overrides each named contribution in that applied template. Omitting it preserves each template contribution's own delta.

The same optional literal `delta_permille` is accepted directly on a modifier apply operation. Equipment bindings on operations may use `$owner`, `$source`, `$target` or `$hit` where the native context provides them. Other equipment bindings name an actual equipment ID.

Modifier apply operations may declare `role`. An omitted or empty role applies all template contributions. An explicit role selects that template's matching contribution roles; `$role` selects the native role supplied by the adapter. Duty-officer applications use `equipment: "$target"` and `role: "$role"` so one multi-role template can apply to the correct systems through native duty bindings. Role filtering happens before target compatibility and application, through the same transaction used by other modifier grants.

A local `remove` identifies a grant created by that rule execution, preserving independent binding identities. An active native effect can instead remove its own logical application explicitly:

```json
{ "kind": "remove", "target": "owner", "scope": "owner" }
```

Owner scope omits `grant`, `effect`, `equipment`, `officer` and `all_sources`. It uses the native owner's frozen application identity, rather than an originating gun's identity or another binding's grant namespace. Equipment, officer, region and projectile payload sources have no implicit native effect application; owner removal fails if no actual owning native application exists. Local scope remains the default.

That identity includes the native effect kind and instance ID as well as its host and grant provenance. Reusing a grant name on another system or hit context does not make that sibling application an owner-removal target.

`energy` optionally computes an existing operations group's entry energy charge, in `energy` units, from 0 to 1,000,000,000. It requires that group's explicit equipment `payer`.

## Required costs and ongoing funding

Entry costs are separate from operation outputs. All required costs and operations prepare together; an execution cannot manufacture its own payment by adding stock earlier in the group. Failed arithmetic, payment or required operations leave the execution uncommitted.

```json
{
  "costs": [
    {
      "payer": "source",
      "equipment": "$source",
      "store": "power",
      "resource": "test.energy",
      "amount": { "value": 20, "unit": "resource:test.energy" }
    }
  ]
}
```

The payer defaults to `target`; `equipment` is optional. `store` and a known `resource` are required. An operations group may also declare literal costs with the same fields and an integer `amount`. Combined static group and computed rule entry costs are limited to 32.

Periodic and maintained rules may declare independently accounted ongoing funding:

```json
{
  "funding": {
    "shortage": "pause",
    "costs": [
      {
        "payer": "owner",
        "equipment": "$owner",
        "store": "power",
        "resource": "test.energy",
        "per_second": { "value": 10, "unit": "resource:test.energy" }
      }
    ]
  }
}
```

Funding has 1–32 required rate entries. Each `per_second` expression uses its resource's quantity unit and the native integer rate contract. Stable named consumers participate in existing funding rather than creating equipment or parallel balances. Native remainders preserve fractional rates across ticks and restoration.

`pause` preserves owned state while withholding unpaid output. `stop` withdraws grants and requires a fresh eligibility transition before automatic restart. `proportional` is supported only for maintained rules whose operations all apply modifiers: native deltas, or explicit overrides, scale once by the funded output. Shields, ability grants, damage and other consequences do not acquire proportional semantics merely by choosing that policy.

Periodic conditions filter scheduled occurrences without restarting the ordinary cadence: an unmatched due occurrence advances the clock once, and a later true condition retains that calendar. Rearming a rule stopped by shortage starts a new full period. Neither case produces catch-up bursts.

A funded maintained rule with an entry charge pays that charge and installs its owned native applications once, with zero operational output until its first paid lease. Failed entry payment does not start recurring upkeep purchases. Its reserve and finite expiry begin at that successful activation. Without an entry charge, installation may wait for the first positive paid lease. Subsequent maintenance never repeats entry charges, refreshes finite lifetimes or recreates an externally removed application. It only updates supported values on existing modifier contributions. A changed resolved target withdraws the previous application and starts a fresh application on the new target.

Electrical entry payment also requires the native reserve's available operating budget. A newly loaded reserve has no fabricated discharge allowance: maintained startup can wait until ordinary resource phases establish an available payment frontier, even when stored energy already covers the entry amount.

## Scheduling and continuation

Native adapters preserve their phase order and original damage/geometry contracts. Context is frozen for its scheduled phase. Pre-damage preparation must commit once before native sharing; repeated quotes do not replay rule evaluation or payment. Secondary damage remains scheduled. Work created while draining a phase cannot recursively execute within that drain.

Continuation stores identities, clocks, captured facts, pending executions and funding remainders. It does not duplicate native reserves or infer new activations on restore. Region restore preserves membership without replaying entry. Despawn retires membership without inventing a movement exit. The migration advances the world/session schema; incompatible saves require a new game.

Modifier continuation stores actual live deltas and lifetime ownership, including scaled or explicitly overridden values and native role-filtered subsets. Independent applications retain their existing 8192-row capacity. Maintained applications have a separate bounded capacity derived from the shared clock, operation and template-row limits, so formerly derived officer buffs do not consume the independent-application allowance.

## Native timing and continuation

Initial maintained source contributions are materialized when a new world is loaded; restoring a world installs its saved contributions and clocks without executing them again. An upkeep payment at tick N buys maintained output for the next native phase, N+1. Current native prices and values remain fixed between allocation quotation and actuation. Pause keeps the same contribution and reserve identities; stop requires a new authored/native eligibility transition before restarting.

Region enter/exit events follow ordered intervals of the actual movement segment and drain after motion in the same tick. Maintained occupancy follows the sampled endpoint. Initial and newly spawned occupants emit one entry; restoring membership emits none. Periodic clocks advance once per due evaluation and do not catch up in a burst.

Damage post hooks, payload reactions, destruction rules and secondary damage use the shared bounded queue. Native destruction's existing processed flag emits its rules once. Its event instigator is the destroyed native actor; it does not invent killer attribution. Impact and missile destruction payloads retain separate source-fact snapshots captured at launch. Entity explosions capture their own context at native destruction.

World and portable session saves use schema 27. They retain rule-owned clocks, credits, next-phase leases, membership, launch context, pending jobs and bounded receipts. Retired shield/payload/periodic execution records are no longer accepted.
