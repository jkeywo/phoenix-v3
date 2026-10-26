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

## Triggers and phases

`trigger` defaults to `activate`. Supported names are `activate`, `deactivate`, `tick`, `enter`, `exit`, `occupancy`, `maintain`, `pre_shield_damage`, `post_shield_damage`, `pre_compartment_damage`, `post_compartment_damage`, `pre_system_damage`, `post_system_damage` and `destroy`. A binding must use a trigger supported by its native source.

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
| `event.inflicted`                                                     | Integer, `damage`               | Post compartment/system damage           |

Examples:

```json
{ "fact": "source.property", "property": "speed" }
```

```json
{ "fact": "target.resource", "store": "magazine", "resource": "test.ammo" }
```

Property facts use the existing supported native property descriptors and their units. They do not expose arbitrary C++ fields or create mutable properties. Equipment-sensitive facts resolve the equipment supplied by the role's native source or binding.

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

## Scheduling and continuation

Native adapters preserve their phase order and original damage/geometry contracts. Context is frozen for its scheduled phase. Pre-damage preparation must commit once before native sharing; repeated quotes do not replay rule evaluation or payment. Secondary damage remains scheduled. Work created while draining a phase cannot recursively execute within that drain.

Continuation stores identities, clocks, captured facts, pending executions and funding remainders. It does not duplicate native reserves or infer new activations on restore. Region restore preserves membership without replaying entry. Despawn retires membership without inventing a movement exit. The migration advances the world/session schema; incompatible saves require a new game.
