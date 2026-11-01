# Carried craft and ship transport

Shuttles, interceptors and bombers are ordinary entities with small fittings, resource networks, structural condition and Console Controllers. A bay stores the same entities dormant between sorties. Docking preserves damage, faults, energy and ammunition; it does not replace or automatically repair a craft.

## Operating craft

The owning Console opens **Settings → Flight operations**. Each craft has its own identity, condition, energy and ammunition report. Utility shuttles have no default combat order. Interceptors defend their carrier or escort a selected contact, prioritising observed incoming missiles through ordinary point defence. Bombers expend their loaded missile salvo against an accepted hostile contact. The Bay owner recalls them from reported expenditure; docked combat craft wait for fresh orders.

Recall supersedes a sortie. Remote orders wait for a working gameplay radio link. Craft continue accepted work during link loss, and the panel labels last reports with their sample tick. Escorts guard their anchor's last reported position when it leaves observation. The Bay policy may request recall from permitted reserve or ageing-target reports; flight cannot invent a recall during link loss. Craft cannot infer unobserved destruction or departure. Returning craft use retained rendezvous information when their carrier becomes unavailable.

Operational FTL preparation is a scoped readiness circumstance. Delegated Bay and Dispatch Controllers score recalls and replacement pickup reservations through ordinary admission; a human owner issues those operations explicitly. Fresh sorties are refused while any FTL drive remains enabled; recovery pickups may still launch. Disabling preparation does not resume old orders. FTL can leave before recovery, with visible warnings and departure-report dispositions. Deployed entities remain in the continuing world; docked craft travel with the carrier. Carrier destruction destroys docked craft.

## Craft and bay authoring

Entity `craft` metadata specifies `role` (`shuttle`, `interceptor`, `bomber`, `boarding_torpedo`), `vertical_speed_mm_s`, `vertical_acceleration_mm_s2`, `cruise_altitude_mm`, `engagement_range_mm` and `return_reserve_permille`. Ordinary Controller trees read accepted-order navigation through `craft.speed`, `craft.heading`, `craft.altitude`, `craft.target`, `craft.x`, `craft.y` and `craft.navigate`, then write their owned channels. The Engine action `altitude_mm` controls vertical flight. Radio, sensors, weapons and magazines retain ordinary ownership and feed requirements.

Sortie sequencing uses the shared scored-objective arbiter and existing Controller action executor. Bay policies score defend, bomb and recall from frozen craft telemetry and permitted accepted targets. Flight policies score observed engagement/interception candidates within the accepted order's scope, and execute its navigation guidance. Weapon and ammunition feeds must be authored explicitly. Closest approach and time to approach use observed missile geometry, not actual projectile target identity. The retired `craft.return_home` action and `craft_return` capability are unsupported: only the Bay owner selects recall.

Entity `craft.guidance_console` names the Console supplying current flight guidance evidence. The loader can infer it only when the fitted motion channels identify one Console uniquely. Console ordering cannot choose the evidence route. Admission captures permitted target positions; finite transport execution reuses those accepted samples. Escort threat range uses current or retained permitted position samples. Native bay and flight machinery do not choose strategic targets or weapon modes.

Ordinary Follow and craft guidance share the planar pursuit calculation. All transport methods share away-party deployment and recovery finalizers. Independent craft radio orders and Comms use the same exact endpoint reachability predicate. Away-party recovery orders use the shared pickup adapter, including legacy craft without a fitted radio. Only bays retain servicing-rate remainders; obsolete per-craft remainder keys in saves are ignored.

Bay scoring declares target-evidence age limits and recall conditions in authored content. Missing evidence can make a new sortie infeasible without completing its objective. Private evidence is visible only to its addressed recipient.

A fitted `shuttle_bay` declares:

- `compartment`, `slots` and `initial`, an array of craft entity definitions. Repeated definitions create separate craft. Nested carried bays are unsupported.
- `transfer_ticks`, `docking_range_mm` and `docking_speed_mm_s`. Launch and recovery share a serial bay operation. Deployed craft retain their home slots; recovery requires physical proximity and acceptable relative velocity.
- Optional `flight_spacing_mm` (default zero) separates the bay's stable slots into altitude bands, with lower slot indices higher. Ordinary navigation and tactical pursuit share those bands. Recovery keeps separated approaches above the carrier, bounded by its docking envelope. Configure spacing and range for the fitted craft dimensions; zero preserves existing content's flight altitudes.
- Optional `power_store` and `charge_per_second`. Charging transfers actual carrier energy subject to source discharge and receiving-store limits.
- Optional `ammunition` connections with `source`, `destination` and `rounds_per_second`. Carrier/craft magazines must share a nonempty `ammunition_type`. Transfers consume finite stock and respect capacity and loaded-round return reservations. Connections skip craft without that destination magazine. Bay throughput is shared in stable craft identity order.

Disabled or damaged bays pause operations. Deployed craft retain ordinary power, heat, weapons and damage behavior. Physical movement, detection/communication distance and combat geometry include altitude; existing ships remain planar by default.

## Away transport

Away assignments offer displacement, available individual shuttles and **Own ship — docking**. Large-ship transport requires `allow_ship_transport: true` and `docking_ports` naming acceptable ports on the target. Other missions leave it disabled and may supply a `ship_transport_reason`, for example atmospheric access. `deployment_range_mm` defaults to 20000 and `deployment_ticks` to 20.

Selecting the ship never issues navigation commands. Helm completes docking at a mission-compatible port before Send or pickup is accepted. Optional docking assistance and manual takeover use the ordinary flight Controllers. Ship transport uses no bay slot. See [Storage, logistics and docking](storage-logistics.md). Shuttle passengers stage at the bay, embark, travel, disembark and are recovered only after collection and docking.

Entity `transport` metadata provides `capacity`, an optional personnel `compartment`, `capabilities` tags and `capability_systems` bindings from tags to required fitted Systems. Capacity counts teams, individual officers and accompanying officers, matching the displacement manifest convention.

Away trees declare `transport_capabilities`. For each tag they can read `transport.capability.<tag>` and `transport.operational.<tag>`; operational capability additionally requires presence and working bound equipment. `transport_present` and `transport_available` expose physical presence and availability. These observations serve mission execution without publishing remote progress to players.

Delivered shuttles remain committed by default. `release_transport("reason")` permits an independent party to release its shuttle for other work. A different available compatible shuttle may later collect it. Released transports no longer provide operational capability to that party. Work persists through recovery. Craft destruction loses its passengers; destination destruction affects parties actually delivered there.

Human `craft.defend`, `craft.escort`, `craft.bomb` and `craft.recall` commands use the craft identity as `system`, observed revision as `value`, and normal Controller identity/generation. Targeted orders carry an entity ID as decimal `text`. AI actions take `(craft identity text, target text, revision integer)` through the same admission path. Existing away commands retain their interface; transport text is a displacement System ID, `ship` or `craft:<entity ID>`.

Runtime/session schema 50 requires new sessions for earlier checkpoints and preserves existing files. Craft-bearing saves retain identities, bay membership, accepted orders, scoped guidance, passenger commitments and servicing records. Boarding torpedoes have a terminal `consumed` phase and require another method for party extraction.

## Exercise and validation

Build `development_packs`, launch native with `--base build/native/test.zip`, and choose **Carrier exercise** / **Carrier exercise watch**. Helm owns flight operations, Dispatch and manual flight; Engineering contributes the repair team. Demonstrate shuttle delivery, authored release and pickup with another shuttle; repeat using the manually flown ship. Launch interceptor defence/escort and a bomber attack, inspect docked servicing and condition, then charge FTL to request recall.

`system.craft` uses independent synthetic fixtures through production loading, admission, execution, network projection and save/recovery. Graphical journeys, real screen-reader use, physical-device input and performance remain separate manually invoked evidence.

Validation on 21 October 2026: the complete native build and all 30 native system cases passed (three filesystem cases were rerun with normal filesystem access after sandbox failures). The final affected group of eight native cases passed. Native and browser player/tool targets compile; WASM away, combat, FTL and Controller cases pass. The development pack verifies through the production loader. Craft continuation compares native and WASM state after excluding archive hashes, following the existing continuation-test convention.

The [native carrier UI journey](smoke/carrier-exercise.md) now records delivery, release, replacement pickup, combat sorties, servicing, FTL recall and manually flown ship transport, including fixes found during the run. Browser interaction, actual screen-reader operation and physical six-display performance remain separate.
