# Resource network contract

`phoenix.resources` compiles a resolved authored `resources` object once into a typed `ResourceNetworkDefinition`. Simulation owns a `ResourceNetwork` component. Definition compilation lives in `resource_definitions.cpp`; the numeric implementation lives in `resources.cpp` so content/Workshop can link verification without the authoritative solver. The module has no Flecs, renderer, UI or network dependency; the authoritative world admits commands, binds fitted System instances to numeric resource indices, requests operation and applies only the resulting paid grants. JSON and identifier lookup are loader/save boundaries, never per-tick dispatch.

The private simulation resource module owns `ShipResourceAllocation`: one candidate tick bound to its originating `ShipResources`. Flight, passive equipment, combat and FTL submit demand first; boost/impulse reserve their target rates through `reserve_system_resource_rate`. The world supplies unavailable fitted-System slots, then reads the allocation's grants to prepare modifiers and commits before actuation. It does not copy solver state, tick a separate preview or temporarily overwrite live supply controls.

Allocation applies unavailable generator, store, conduit and cooling-link overrides to its candidate, advances the solver once and restores requested enabled/isolation settings within that candidate. Actual quantities, telemetry, grants, fractional remainders and discrete commitments remain those of the unavailable physical tick. Preview never mutates the originating ship; abandoning the allocation leaves it unchanged. Commit moves that same result into its origin once. Allocations cannot be copied or moved, and another committed resource tick invalidates an older allocation. Demand and resource commands must be complete before allocation and remain frozen until commit; mechanism-specific effects follow afterwards. The transient allocation is not serialized and changes no save schema.

## Units and authoring

All quantities are nonnegative integers in the pack's consistent energy, heat and coolant units. Rates are units per simulation second. The runtime executes at 20 Hz, preserving division remainders in recovery. Individual authored amounts/rates are bounded at 1,000,000,000,000. `*_ppm` values use parts per million, from zero to 1,000,000; overdrive multipliers use permille, from 1,000 to 10,000. Compilation sorts each record family by stable ID before assigning numeric indices; reordering records does not change allocation tie-breaks or the definition signature. IDs are unique within each array; references must resolve. At most 4,096 records are accepted per array.

```json
{
  "stores": [
    {
      "id": "reactor-capacitor",
      "capacity": 1000,
      "initial": 500,
      "charge_rate": 200,
      "discharge_rate": 200,
      "stop_heat": 1000,
      "recover_heat": 600,
      "passive_cooling": 2
    },
    {
      "id": "engine-battery",
      "capacity": 200,
      "initial": 100,
      "charge_rate": 100,
      "discharge_rate": 100,
      "group": "propulsion",
      "operation_heat_ppm": 500000,
      "stop_heat": 200,
      "recover_heat": 100
    }
  ],
  "generators": [
    {
      "id": "generator",
      "store": "reactor-capacitor",
      "maximum_rate": 200,
      "response_rate": 100,
      "initial_output": 100,
      "heat_ppm": 100000
    }
  ],
  "groups": [
    {
      "id": "propulsion",
      "budget_rate": 100,
      "restore_margin_rate": 10,
      "restore_ticks": 20
    }
  ],
  "links": [
    {
      "id": "main-feed",
      "from": "reactor-capacitor",
      "to": "engine-battery",
      "capacity_rate": 100
    }
  ],
  "coolant_sources": [
    {
      "id": "coolant-tank",
      "initial": 1000,
      "removal_rate": 30,
      "coolant_per_heat_ppm": 100000
    }
  ],
  "cooling_links": [
    {
      "id": "engine-cooling",
      "source": "coolant-tank",
      "store": "engine-battery",
      "capacity_rate": 30,
      "allocation_rate": 20
    }
  ]
}
```

The numbers above describe the schema; shipped balance is authored elsewhere.

| Record         | Additional supported fields                                                                                                                                                                                                                                   |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Store          | `enabled` (default true), `loss_empty_ppm`, `loss_full_ppm`, `initial_heat`, `charge_heat_ppm`, `depletion_heat_ppm`, `overdrive_permille`/`overdrive_heat_permille` (default 1000), `stress_rate`, `override_stress_rate`, `stress_limit`, `allows_override` |
| Generator      | `enabled` (default true); maximum/response must be positive, initial output cannot exceed maximum                                                                                                                                                             |
| Coolant source | Optional `power_store` names the dedicated pump reserve; without it the provider is explicitly passive                                                                                                                                                        |
| Group          | Optional `parent`; fine groups subdivide ancestor reservations; cycles are refused                                                                                                                                                                            |
| Power link     | `isolated` (default false); directed links can share endpoints and form redundant routes                                                                                                                                                                      |
| Cooling link   | `isolated` (default false); allocation cannot exceed link capacity or overbook its source                                                                                                                                                                     |

Omitted numeric fields default to zero except the documented ratios, restore duration (one tick) and multipliers. Store capacity must be positive. A zero thermal-stop threshold disables thermal protection; otherwise recovery must be strictly lower. Stress-producing modes require a finite positive stress limit. Coolant consumption must have a positive ratio; there is no lossless recycling. A generator's store is its actual capacitor: no third mandatory battery is invented. Optional extra stores use the same schema. The encompassing equipment loader additionally verifies fitted ownership, required supply paths and declared starting readiness.

## Numeric execution and paid work

Resolve IDs through the definition's `*_ids` maps during construction. Runtime APIs accept `ResourceIndex`; out-of-range indices refuse through checked access. Disabled, thermally stopped and stress-limited stores neither source nor receive energy transfers; active thermal overrides obey their authored permission. Cooling continues independently. `request_operation(store, rate, discrete_energy)` sets the standing continuous request and optionally requests one atomic discrete action for the upcoming tick. Calling it again replaces that tick's intent. Continuous requests persist; discrete requests clear at the tick boundary and must be explicitly renewed while waiting.

After `tick()`, `telemetry()[store].consumed` is paid energy, including discrete energy only when `discrete_paid` is true. Callers derive continuous paid energy by subtracting the discrete request when paid. No caller may apply a discrete effect before that result. Output scales to the actual grant; it does not scale merely because Engineering raised incoming allocation.

Sub-unit continuous work uses prepaid credit at `resource_work_unit` (20,000 fractional units per energy unit). A nonzero request pays the next whole energy unit before its first effect, consumes only the required fraction and retains the unused fraction in the store. Pausing preserves that credit; resuming spends it before charging again. Denied work creates no credit or deferred right to output. `work_requested` and `work_funded` expose the precise continuous work fraction, independently of the whole energy debit in the current tick. The authoritative adapter and powered coolant use their ratio for actual output, including ticks funded by an earlier debit. Consumers on the same reserve share one conserved credit pool; each consumer cannot recreate that pool independently. Credit cannot be transferred or refunded as reserve energy. Disabled/protected stores cannot spend it. Save/restore retains the pool and validates that ordinary unused credit is less than one energy unit; older saves' accrued unpaid remainders migrate into a liability that must be settled before new work.

The tick pays affordable continuous work from local reserves, accepts finite generation, resolves group cuts/restoration, routes incoming energy and completes affordable pending operation. Transfers never exceed source energy, source discharge, receiving charge/headroom, residual conduit capacity or any ancestor group's remaining budget. Residual edge capacity is shared by every path. A residual maximum-flow graph accounts for shared edges once and can reroute an earlier path to satisfy a later recipient through a redundant route. Stable-ID order breaks equivalent path ties. Intermediate conduit vertices represent pass-through routing; charging/discharging limits apply to the actual source/recipient stores, not to artificial intermediate battery transfers.

Distribution serves unmet work before reserve replenishment, then lower reserve fullness, with stable order for ties. It stays inside each group reservation. Unused budget is not reassigned to another group. Root-group supply considers sustainable per-second exporting rates and actual conduit feasibility; fine groups reserve subdivisions of their parent's budget. Fractional tick credit does not make a sustainable low-rate source appear unavailable between integer transfers. A recipient's existing reserve cannot count as incoming supply through a cyclic route. Actual flow removes directed circulations and cancels local source/recipient overlap before debiting energy or recording transfer heat. Oversubscription randomly cuts currently supplied sibling groups; restoration randomly chooses sustainably eligible cut groups after authored margin/duration. No priority, age or critical-System exception affects cut eligibility. Manual budgets remain intact, and a cut stops incoming allocation rather than local operation. Conduit feasibility is also checked before restoring a cut reservation.

Generator response is finite. Its commanded and ramped rates are distinct from accepted energy generation. Full buffers do not draw unaccepted energy; thermal protection or disable suppresses generation immediately. Local storage depletion never adds a restart delay. Thermal stops clear at the lower threshold while current enabled state and requests remain authoritative.

Loss interpolates between empty/full authored ratios using current fullness. Charging, paid operation and discharge contribute heat. Overdrive increases demand/operating heat and authored stress. Fractional overdrive demand uses the same prepaid work accounting: a one-unit-per-second request at 150 percent pays three units over two seconds. The flight integration applies the paid output multiplier to acceleration, turn and impulse preparation while preserving target-speed and mode limits. Its external heat applies both output and authored overdrive heat multipliers; constrained supply scales the resulting effect, so doubling requested output with half payment grants ordinary output. A supported thermal override may continue through a thermal stop and accrue additional stress. Reaching the finite stress limit suppresses operation; this module does not repair stress or invent random damage. Passive rejection and active cooling remove actual local heat. Heat/stress accumulation is bounded to the supported integer domain.

Cooling allocation reserves provider removal capacity, including unused reservations. Delivery cannot exceed a link's capacity/allocation or finite remaining coolant. Coolant expenditure preserves fractional consumption, and only actual heat removal spends stock. Isolating a link does not lend its reserved capacity to another recipient. Reallocation is explicit through `set_cooling`; overbooking is refused transactionally.

A source with `power_store` scales its removal capacity by continuous pump work actually paid that tick; an unpaid or disabled pump supplies no cooling. Payment precedes heat removal, including when the pump cools its own reserve. The fitted verifier requires exactly one positive-rate consumer on that reserve, of kind `coolant`, so unrelated work cannot pay for a disabled pump. Shared consumer reserves elsewhere remain legal. A provider without `power_store` declares passive circulation with finite authored removal and coolant stock; powered circulation must bind its reserve explicitly.

## Controls, telemetry and continuity

Setters cover store enabled/overdrive/thermal override, mutable charge/discharge limits, generator output/enabled, group budgets, conduit isolation and cooling allocation/isolation. `set_storage_limits` cannot exceed the immutable authored charge/discharge maxima. `apply_operating_heat(store, paid_heat)` accepts bounded nonnegative heat from already paid external mechanisms, immediately applies the thermal-stop threshold and preserves enabled/stress protection; it does not run extra cooling. The flight adapter retains its own fractional heat remainder.

The authoritative integration enforces operator permissions before these setters. Storage Systems control their bound store's enabled/charge/discharge state; Generator Systems control their bound provider; Distribution Systems control only their named group/conduit; Coolant Systems control only their named receiving link. Endpoint writers are unique. Thermal override and overdrive belong to an explicit Storage controller when present, otherwise its Generator controller, otherwise the sole bound consumer. Multiple consumers may share finite reserves across Console boundaries without receiving authority over common store settings. Per-consumer enabled state remains independent of store enabled state. Machinery/service ownership is supplied by fitted-System bindings; the numeric solver does not infer authority from network connectivity.

The shared fitted verifier checks explicit `power_store` for positive energy/discrete costs or flight heat costs, references and capability ownership, atomic discharge feasibility, and initial supply/readiness. An intentionally unpowered initial consumer must declare Boolean `powerless_start: true`. Initial readiness requires usable local reserves or an enabled reachable exporting source; disabled or initially thermal-stopped stores, isolated routes and zero incoming ancestor reservations cannot establish readiness. These are connection checks, not balance or exact shipped-performance assertions.

Public typed state exposes stores, generators, groups, conduits and coolant. Per-tick telemetry exposes requested/paid operation, accepted incoming/generation, outgoing/loss, heat added/removed, limitation and discrete payment. `forecast()` extrapolates the last committed net energy/heat into depletion, fullness, thermal-stop and thermal-recovery ticks. `coolant_forecast()` extrapolates actual provider heat removal and its coolant ratio, including fractional liability that has not yet become a whole-stock debit. A zero forecast means the boundary is already reached; a negative value means no boundary is predicted under those constant last-tick assumptions. Recovery forecasts describe physical cooling and do not enable deliberately shut-down equipment. Forecasts explicitly exclude future commands, allocation changes and network changes; they are not guaranteed endurance.

`save()` includes definition identity, deterministic random state, clock, all division remainders, controls, cut/restoration state, pending discrete requests, resources and telemetry. `restore()` validates version, definition signature, array rosters, quantities, bounds, supported override modes, random state and shared coolant reservations into a temporary network before replacing live state. Restore therefore cannot partly apply a rejected record. The world save must retain the resolved authored resource definition and the fitted bindings alongside this state.

The private authoritative `gameplay_resources` adapter compiles one numeric binding per fitted System. Requests carry a duty, optional extra quoted rate and pending atomic action; grants expose requested/paid energy and paid supply permille. Consumers sharing a store receive conserved shares of the actual debit, and only one pending atomic action owns that store's discrete payment at a time. Adapter continuation includes a signature over network identity, fitted endpoint bindings, capability kinds, costs and store-mode authority, preventing a save from silently rebinding controls to a changed fit.

Production-loader synthetic integration should cover conservation, shared bottlenecks, redundant routing/isolation, paid continuous/discrete work, random cuts without manual-setting loss, stable restoration, coolant stock accounting, thermal recovery respecting disable, stress limits, and uninterrupted/resumed continuation. Content verification checks connections/readiness; actual ship tuning remains mutable. Damage/repair/bypass construction and recycling/resupply are outside this module's current contract.
