# Custom property authoring

Custom properties extend the shared gameplay catalogue with stored quantities, modifiable numeric attributes, flags and finite enums. This reference describes the authoring contract for the custom-property migration. Validation and manual acceptance are recorded separately from the presence of these fields.

## Catalogue and owner declarations

`definitions.json` accepts a top-level `properties` array. Properties use ordinary `extends` inheritance, globally unique IDs and explicit mod overrides. They cannot redefine a native property such as `recharge`.

```json
{
  "properties": [
    {
      "id": "example.charges",
      "kind": "quantity",
      "label": "Available charges",
      "unit": "charges",
      "minimum": 0,
      "maximum": 12,
      "default": 3,
      "display_scale": 1
    },
    {
      "id": "example.capacity",
      "kind": "attribute",
      "label": "Capacity",
      "unit": "charges",
      "minimum": 0,
      "maximum": 100,
      "default": 12
    },
    {
      "id": "example.enabled",
      "kind": "flag",
      "label": "Enabled",
      "default": true
    },
    {
      "id": "example.state",
      "kind": "enum",
      "label": "State",
      "choices": { "ready": "Ready", "recovering": "Recovering" },
      "default": "ready"
    }
  ]
}
```

Each property requires an explicit default. Numeric values are deterministic integers; `display_scale` affects presentation only. Quantities have nonnegative bounds; attributes may declare a negative minimum. Flags accept JSON Booleans. Enum values must be identifiers from that property's `choices`; the choice labels support ordinary localisation. Flags and enums do not declare numeric units, bounds or display scaling.

Declare properties explicitly on an entity, fitted equipment or equipment template, officer, or combat compartment. Compartment declarations belong on `combat.decks[].compartments[]` records. A declaration's empty object uses the catalogue default; a scalar supplies the initial value:

```json
{
  "properties": {
    "example.charges": {},
    "example.capacity": 10,
    "example.enabled": true,
    "example.state": "ready"
  }
}
```

A missing declaration stays absent. Reading it produces a missing fact; writing, modifying or spending it fails the prepared transaction. Initial values are applied once. Reassignment and continuation retain the current values.

## Reads and expressions

Rules and ability requirements use the existing typed expression language. The default property view is `effective`; `base` reads the stored quantity or unmodified base value. Native numeric property facts also support these views.

```json
{
  "op": "all",
  "args": [
    {
      "op": "ge",
      "args": [
        { "fact": "owner.property", "property": "example.charges" },
        { "value": 1, "unit": "charges" }
      ]
    },
    {
      "op": "eq",
      "args": [
        {
          "fact": "owner.property",
          "property": "example.state",
          "view": "base"
        },
        { "value": "ready", "enum": "example.state" }
      ]
    }
  ]
}
```

Enum literals identify their domain with `enum`. Comparing unrelated enum domains, comparing an enum to an untyped string, enum ordering and enum arithmetic are rejected. Numeric arithmetic retains the existing unit checks. `present` can test a missing property without inventing a value.

## Operations and temporary modifiers

Operation groups add four operation kinds:

```json
{
  "kind": "operations",
  "operations": [
    {
      "kind": "property_subtract",
      "target": "owner",
      "property": "example.charges",
      "amount": 1
    },
    {
      "kind": "property_set",
      "target": "owner",
      "property": "example.state",
      "value": "recovering"
    },
    {
      "kind": "property_rate",
      "target": "owner",
      "property": "example.charges",
      "amount": 2,
      "saturate": true
    }
  ]
}
```

`property_add` and `property_subtract` use nonnegative `amount`; `property_rate` accepts a signed per-second amount and carries fractional units between calls. A rate operation performs one native tick of work per invocation, so a continuous rate rule normally uses `period_ticks: 1`. Distinct application/operation identities retain independent remainders.

`property_set` writes a typed base value. Numeric add/subtract/rate operations reject out-of-bounds results by default; `saturate: true` explicitly clamps them to declared bounds. Costs always require the full amount. Property operations accept the ordinary context target roles plus optional equipment or officer selection. Rule `values` can compute an operation's numeric `amount`, or the typed `value` of a `property_set`.

Property writes participate in the existing prepared transaction. A failed required operation, payment, owner lookup or value check leaves the batch unchanged and emits no property-change events.

Attributes accept the existing additive percentage modifier contributions. Quantities cannot receive percentage modifiers or temporary overrides. Flag and enum contributions supply a typed `value` and optional integer `priority` instead of a percentage:

```json
{
  "id": "example.lockout",
  "kind": "modifier",
  "duration_ticks": 100,
  "contributions": {
    "state": {
      "property": "example.state",
      "value": "recovering",
      "priority": 20
    }
  }
}
```

The highest priority wins; equal priorities use the newest distinct application. Refreshing the same application preserves its precedence. Removing an override reveals the next override or the current base. Base writes made while an override masks them are retained.

## Payments, requirements and completed work

Explicit effect costs select either a store/resource or a quantity property:

```json
{
  "payer": "owner",
  "property": "example.charges",
  "amount": 1
}
```

Rule funding uses the same selection with a typed `per_second` expression. Ability `costs` use constant `per_second` amounts. Property and store costs can share a bundle and compete for the existing funding budget. Their quotation and prepaid fractional credits follow the shared work accounting. An operation's outputs cannot pay its own input costs.

Abilities can declare a Boolean `requirements` expression, a `family`, and `requirement_failure: "end_retain_intent"`. Requirements are checked at admission, preparation completion and sustained operating boundaries. Failure ends the current execution normally while retaining a sustained request; recovery starts a fresh activation when requirements, cooldowns and funding permit it.

`{ "fact": "owner.completed_work", "family": "example.family" }` reads the frozen actual output of the owner's completed ability work. Its unit is `permille`; simultaneous matching abilities add their output. This fact measures actual output after funding and faults, whereas a cost purchases funded work. Rules that gain strain and rules that recover it inspect the same completed-work facts.

Officer rules normally require duty eligibility. Explicit officer-owned bindings may set `eligibility: "lifetime"` to run for a living officer while off duty or travelling. This does not change ordinary duty buff eligibility.

## Change events and observations

`property_changed` is an instant trigger delivered through the shared bounded rule queue. It runs after a successful transaction with one event per changed owner/property, using the transaction's initial and final values. It does not recursively execute another event within the same drain.

Event facts select a known property and a view:

```json
{
  "op": "ne",
  "args": [
    {
      "fact": "event.property_before",
      "property": "example.state",
      "view": "effective"
    },
    {
      "fact": "event.property_after",
      "property": "example.state",
      "view": "effective"
    }
  ]
}
```

Property modifiers also emit events when they change the effective value. Refreshing only a duration does not. Masked base changes remain visible to base-view event facts.

An ability's `observations` array explicitly lists the granting source's properties to publish. Existing Console/equipment-feed and personnel department permissions still apply. Published rows contain the effective value and presentation metadata; they do not disclose private base values or override provenance. Controller trees can add `ability_property_bindings` entries with `endpoint` and `property`, then read `ability.<endpoint>.property.<property-id>` from the same permitted frozen observation.

Workshop's property template, owner declaration, effect, ability and Controller inspectors edit these fields through the ordinary verification and export flow. Save continuation preserves stored bases, contribution precedence, fractional work and queued events. Incompatible older schemas require a new session.
