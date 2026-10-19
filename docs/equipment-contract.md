# Reusable equipment and assembly composition

The production pack resolver accepts global `equipment` and `assemblies` definition arrays beside entities, maps, Consoles and trees. Their IDs share the normal global definition namespace, pack dependency/override rules and `extends` merge-patch inheritance. Inheritance resolves before `resolve_equipment(Catalog&)` expands fitted placements. The resulting entity uses ordinary canonical `systems`, `resources`, `controllers` and `services`; per-tick execution uses compiled identities and never revisits JSON composition.

## Equipment templates

An equipment definition supplies a reusable fitted-System specification. An entity or assembly System uses `template` and local overrides:

```json
{
  "equipment": [
    {
      "id": "library.engine",
      "kind": "engine",
      "max_speed_mm_s": 20000,
      "acceleration_mm_s2": 1000,
      "power_store": "battery"
    }
  ],
  "entities": [
    {
      "id": "example",
      "systems": {
        "port-engine": {
          "template": "library.engine",
          "max_speed_mm_s": 18000,
          "power_store": "port-battery"
        }
      }
    }
  ]
}
```

This is a composition fragment; the entity still needs its normal rendering, ownership, resource and scenario definitions. Equipment `id` and `extends` are removed from the fitted specification. `template` selects the base; all remaining instance fields apply as a JSON merge patch, including explicit removal with null. The resolved System requires an explicit `kind`. Ordinary inline Systems remain supported. Equipment-to-equipment reuse uses `extends`; a nested `template` field in a global equipment definition is refused.

A template's `controller` is a default tree binding. The entity's explicit `controllers` binding wins; an assembly's controller default wins over its equipment default. Referenced tree IDs remain global and are validated normally. The resolver retains `_equipment_origin` with the selected template and instance overrides. Templates do not copy shared providers merely because another System refers to them.

Unused templates are verified too. Concrete equipment requires a supported explicit kind and its primitive capability parameters: positive speed/acceleration for engine, lateral, boost and impulse equipment; positive turn rate for steering; positive range for radar. Numeric fields, Boolean fields, typed references and default controllers are checked before instantiation. Deliberately partial abstract equipment is not supported in this phase; inherit a concrete definition and override it. Unused assemblies compile their resource graph against declared external service placeholders, so unresolved local endpoints cannot hide in an unplaced library entry.

## Assemblies and independent placements

An assembly groups local Systems, resources and service declarations:

```json
{
  "id": "library.drive-unit",
  "systems": {
    "engine": {
      "template": "library.engine",
      "power_store": "battery"
    }
  },
  "resources": {
    "groups": [{ "id": "drive", "budget_rate": 50 }],
    "stores": [
      {
        "id": "battery",
        "capacity": 100,
        "initial": 50,
        "charge_rate": 50,
        "discharge_rate": 50,
        "group": "drive"
      }
    ],
    "links": [
      {
        "id": "feed",
        "from": "$supply",
        "to": "battery",
        "capacity_rate": 50
      }
    ]
  },
  "services": {
    "supply": { "kind": "power", "direction": "input" },
    "local-reserve": {
      "kind": "power",
      "direction": "output",
      "target": "battery"
    }
  }
}
```

The entity declares the external supply once and places the assembly repeatedly:

```json
{
  "services": {
    "main-power": {
      "kind": "power",
      "direction": "output",
      "target": "reactor-capacitor"
    }
  },
  "assemblies": [
    {
      "id": "port",
      "assembly": "library.drive-unit",
      "bindings": { "supply": "main-power" }
    },
    {
      "id": "starboard",
      "assembly": "library.drive-unit",
      "bindings": { "supply": "main-power" },
      "overrides": {
        "systems": { "engine": { "acceleration_mm_s2": 800 } }
      }
    }
  ]
}
```

The entity's root resources must define `reactor-capacitor`. The canonical Systems become `port.engine` and `starboard.engine`; stores become `port.battery` and `starboard.battery`. Both feed links refer to the same real root capacitor, whose charge/discharge and shared path limits remain singular. Each local battery, group and controller instance is independent. Console ownership binds these explicit placed System IDs through the existing Station/Console definitions.

Placement `overrides` is a merge patch of the resolved assembly. Object members merge; resource arrays replace as whole arrays under standard JSON merge-patch rules. The resolver refuses duplicate placements, collisions with existing System/resource identities, invalid IDs, unsupported nested assemblies and unresolved template references. Prefixing must remain inside the normal 128-character identifier bound. Assembly nesting is explicitly unsupported, so recursive assembly cycles cannot execute; ordinary `extends` cycles are rejected by inheritance resolution.

Expansion checks prospective sizes before cloning placement definitions: at most 256 fitted Systems, 4,096 services and 4,096 records in each resource family per resolved entity. Overrides use object merge and whole-array replacement counts rather than multiplying unbounded repeated JSON before detecting the limit.

Components, command channels, physical attachment/compartment data, localisation and other mechanism properties stay in their authored System specifications. Composition does not invent a damage model, infer control ownership or treat a conduit as a walkable route.

## Explicit services and reference rewriting

Service declarations use `kind` (`power`, `coolant`, `information`), `direction` (`input`, `output`) and optionally `target`. An entity service must resolve to a real endpoint. An assembly declaration without a target is an import requiring an explicit placement binding. The binding's external service must have the same kind and opposite direction. Missing, ambiguous and incompatible bindings fail; they never become broadcasts or implicit global search.

Assembly declarations with targets expose `placement.service-name` in the entity service table. Their targets receive local prefixes when they name local endpoints. All exposed services are collected before imports resolve, so one placement can bind another placement regardless of declaration order. An unbound declaration with a local target refers to that local endpoint. An explicitly bound declaration resolves `$service-name` to the external endpoint's actual `target`.

Power endpoints target stores. Coolant output endpoints target coolant sources; coolant input endpoints target receiving stores. Information endpoints name fitted Systems or Console blackboard endpoints; their existence and permitted scope are checked by the knowledge/feed adapter rather than inferred from physical resource links. Endpoint aliases and hidden wildcard discovery are unsupported. A service reference does not copy capacity or transfer control authority.

The resolver rewrites only supported identity-bearing fields:

| Context                      | Rewritten references                                                                                                                                  |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Resource records             | Their own `id`; store `group`; generator `store`; power-link `from`/`to`; group `parent`; coolant-source `power_store`; cooling-link `source`/`store` |
| System mechanisms            | `system`, `power_store`, `store`, `cooling_link`, `coolant_source`, `generator`, `group`, `power_group`, `conduit`                                    |
| Assembly controller bindings | Local System keys; tree values retain global identity                                                                                                 |
| Service targets              | Known local stores, coolant sources or Systems according to declared type/direction                                                                   |

Supported System reference fields are visited inside component objects/arrays. An internal local name receives the placement prefix; an already external identity remains explicit. `$name` resolves only through the placement's declared service binding. Normal production resource/System compilation then checks the exact referenced family. Display text, localisation keys, tree IDs, arbitrary metadata and origin records are not blindly searched/replaced.

## Resolved metadata and repeated verification

`resolve_equipment` expands into a temporary entity table and commits only after every composition succeeds. It retains `_assembly_origin` on placed Systems/resources/exposed services and `_equipment_source` on composed entities. The latter contains the original composition fields and is used to rebuild the canonical composition on a subsequent call, making normal repeated verification idempotent. Retained source is itself revalidated; a marker does not bypass expansion or service checks. Entities using neither templates, assemblies nor services remain untouched.

Resolved catalogues are immutable runtime/inspection results. Workshop edits the authored draft and reruns the production resolver; it must not treat a resolved catalog's source metadata as a second editable document. Export writes authored definitions, retaining explicit template/placement intent and overrides. The encompassing catalog validator checks mechanisms, supply paths, tree/Console bindings and ownership after composition. Synthetic production-loader cases should cover inherited templates, repeated placements, shared provider capacity, override precedence, cross-placement services, missing/incompatible bindings, identity collisions and repeated resolution.

When reopening a resolved catalogue for editing, Workshop restores the original `systems`, `resources`, `assemblies`, `services` and `controllers` fields from `_equipment_source` and removes resolution metadata before making edits. Subsequent export/reopen therefore preserves the new authored edits instead of rebuilding stale source retained in a flattened runtime definition.
