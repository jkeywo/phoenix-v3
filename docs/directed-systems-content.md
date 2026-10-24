# Directed systems and payload authoring

Directed capabilities bind reusable effects from the existing effects catalogue. Mod inheritance and explicit override rules apply to modes, target policies, payloads and periodic conditions just as they do to shield and modifier effects. The migration plan remains the acceptance checklist; this document describes the authored format.

A weapon or radar System has a `directed` object with `modes` and `targeting` reference arrays. Optional `initial_mode` and `initial_targeting` name entries in those arrays; otherwise the first entry is selected. Each list has at most 32 entries. Modes and targeting policies are separate selections; the chosen pair must be compatible. The compatibility graph must be connected so every valid selection can be reached through separate mode and targeting commands. An assembly with simultaneous scanning and firing uses two System identities. Each physical turret has one directed targeting owner.

```json
{
  "directed": {
    "modes": ["example.fire", "example.scan"],
    "targeting": ["example.designated"],
    "initial_mode": "example.fire"
  }
}
```

`kind: "targeting_policy"` selects the native `policy` primitive: `designated`, `intercept` or `off`. Its `label` is the user-facing label/localisation key. A `kind: "directed_mode"` effect selects `operation: "launch"`, `"beam"` or `"scan"` and lists compatible targeting-policy IDs in `targeting`. It owns `range_mm`, `energy_per_second`, `discrete_energy`, `auto_energy_per_second`, and the operation parameters. Those costs and parameters are no longer authored inline on weapons. Physical origin, coverage, turret, magazine references and power-store bindings stay on the equipment. Broad radar range and power remain equipment properties; focused acquisition parameters belong to the selected mode. Equipment `broad_capabilities` explicitly lists instantaneous native radar acquisitions as a map of supported capability names to zero. Zero-duration capabilities in a directed scan mode still require that mode and its qualified target; they never become passive facts. Hybrid weapon/scanners have no passive radar acquisition. Active scan `energy_per_second` is an additional demand on top of the native radar baseline. Migrated scanners keep their previous energy demand on the physical radar and use zero additional mode cost, preserving both broad operation and the previous total during scanning.

Launch modes use `ammo_use`, `fire_ticks`, `load_ticks` and optional native `projectile` defaults. Actually loaded resource ammunition profiles retain their native timing/projectile overrides. Beam modes use `damage_per_second`, `penetration`, `damage_per_m` and `shield_bypass_permille` and require positive continuous power. Scan modes use a `capabilities` object mapping the existing native capability names to acquisition ticks. Only launch modes support automatic missile interception; beam and scan modes reject that targeting policy. Scans cannot use weapon payloads. Launch and beam capabilities require native combat structure, including when fitted on radar equipment. Modes with costs require a fitted `power_store`; launch modes require magazines. Every launch mode is checked against each bound magazine.

A mode's `payloads` list adds explicit secondary hit payloads. With `resource_payloads: true` (the default), the using launch mode can carry payloads attached to the actually loaded resource. Resource references remain inert when stored, moved or loaded. Projectile profiles and destruction profiles also accept distinct `payloads` lists; destruction does not inherit an impact payload.

```json
{
  "id": "example.burning-hit",
  "kind": "payload",
  "bindings": [
    {
      "id": "penetrating-burn",
      "outcome": "compartment",
      "positive_damage": true,
      "effect": "example.apply-burning"
    }
  ]
}
```

Payload bindings have stable `id` values and select `outcome: "shield"`, `"compartment"`, `"system"` or `"entity"`. `effect` references an `operations` effect group, with the same prepared operation and funding semantics as other shared effects. `positive_damage` defaults to true; false explicitly allows a reached zero-damage outcome. Native resolution supplies the actual hit context. A shield binding is emitted only for a contribution whose layer is reached; a system binding requires an identified affected System. An operation's `protected` target denotes the actual hit entity. Modifier application can use `equipment: "$hit"` only for a shield or System binding; the runtime resolves the actual contribution/equipment and validates compatibility.

```json
{
  "id": "example.burning",
  "kind": "periodic",
  "duration_ticks": 100,
  "period_ticks": 20,
  "effect": "example.burning-tick"
}
```

Periodic conditions reference an `operations` effect group using `effect`; both duration and period are positive integers bounded by one million ticks, with period no longer than duration. The initial supported payload binding is a compartment. A tick's native damage uses its captured compartment context, without re-entering external shield traversal. Periodic tick groups cannot apply further periodic conditions. This avoids unbounded condition spawning while allowing damage, shared modifiers and resource operations through the common transaction boundary.

Apply a condition using an ordinary `apply` operation with `effect` and stable `grant`. Exact removal uses `remove` with the same grant/source identity. An explicit `remove` operation with `all_sources: true` and a periodic `effect` ID removes that definition from all compartments on the target entity; this form omits equipment and does not need a grant. Catalogue validation rejects `all_sources` for other operation kinds and nonperiodic definitions.

The runtime captures payload parameters at launch and periodic application. Authored content controls binding and execution; catalogue source identity is for lifetime/refresh/receipts, not source authentication. A scenario may invoke the shared operations directly using its own authored target context.
