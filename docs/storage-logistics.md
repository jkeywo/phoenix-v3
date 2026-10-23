# Storage, logistics and docking

The implementation follows GDD sections 9.3–9.4 and 11. Material names are authored identities. Ships, carried craft and facilities use finite stores and the same delivery rules.

## Operating supplies

Open **Console settings → Stores and deliveries**. Engineering sees main and local stocks, targets, waiting requests and physical batches. Other operators see their own stores. Local owners set local targets; Engineering holds main stocks, cancels waiting requests and requests external supply. Receiving space includes committed deliveries and loaded-round return reservations.

Reachable replenishment runs automatically. The first eligible authorised request receives scarce stock, in stable request order. Dispatch removes stock and reserves receiving space atomically. Waiting requests reserve neither. Routes have travel time, handling throughput and concurrency. Damage or deliberate closures strand cargo at its actual location with its progress retained. Teams, personnel and deliveries respect the same deliberate route closures. Compartment state owns these closures independently of inventories. Optional `combat.access_controller` assigns their owner; it defaults to the logistics controller.

Cancelling a waiting request releases its remaining work. Cancelling an in-transit batch requests a physical return while it remains aboard its source, outside an active crossing, and compatible return space is available; it does not refund stock instantly. Cargo that has crossed to another entity remains there until a supported return transfer occurs.

Magazine stores use the existing ammunition state, loading timers and loaded-round reservations. Repair teams carry parts, clinical teams carry supplies, and facilities have local stock. Component and clinical work pays when progress begins; retained task progress prevents payment twice after interruption. Structural work pays per HP with saved fractional credit. Deployed teams consume their own carried kits, independently of access to mothership holds.

## Docking and cargo

Open **Console settings → Docking**. Helm selects a known compatible port and requests a manual or assisted approach. Assistance commits ordinary flight channels through the owning Controllers. It uses real propulsion, power, turning, port alignment and collision geometry, including an approach around a receiving hull where needed. Manual movement cancels an assisted approach. Changing the responsible operator also cancels it.

Both endpoints authorise docking. Ports can accept allies by standing permission or accept a specific known entity. Physical capture begins finite mating work. Docked vessels retain their identities, crews, condition and inventories; a rigid connection follows the receiving vessel, inhibits attached propulsion and accounts for assembly mass and collision impulses. Cyclic docking relationships and double-booked ports are rejected.

Supply service additionally requires both ports to authorise it. Connected allied providers can replenish authorised targets from their actual main stocks. Explicit transfers select a compatible receiving store and quantity. Internal delivery, port crossing and receiving-side delivery all take actual work. Dock throughput is shared between crossing batches.

Ordinary undocking stops new crossings, finishes an active crossing and performs visible separation work. Emergency release requires fitted hardware and applies authored consequences. FTL departure is blocked until docking operations have ended.

The bay owner grants individual shuttles **standing cargo permission** from Stores and deliveries. Engineering can then select an available permitted shuttle, compatible cargo hold, destination store, material and quantity. The same craft loads finite stock, launches, flies, docks, unloads and returns. Cargo commitments prevent passenger/sortie double-booking and protect loaded supplies from later requests. Destination space is reserved before launch. Link loss retains the last reported job phase and report tick; recall/cancellation preserves delivered effects and unused physical cargo.

Large-ship away transport is **Own ship — docking**. Missions must explicitly allow it and name acceptable destination ports. Dispatch never navigates the ship. Deployment and recovery require the correct completed dock and finite personnel crossing work; docking alone does not send a party. The target port supplies the entry compartment. Undocking leaves delivered parties and supplies in place. Shuttle and displacement personnel transport retain their separate insertion rules.

## Authoring and commands

Workshop exposes material stores, consumers, ports and mission docking support through its normal draft/Undo/Verify/Test flow.

Entity `logistics` defines `controller`, `units_per_second`, `concurrency`, `batch_size`, `stores` and optional `consumers`. Resource definitions live in the global catalogue; local `materials` and material-specific volume definitions are retired. A store supplies `id`, `compartment`, common `stock`, optional `controller`, `targets`, `main` and `system`. The nested stock selects a `capacity_tag`, shared `capacity`, acceptance `filter`, initial quantities and optional single-resource selection. The global resource size for that tag determines occupancy.

A magazine-bound logistics store aliases the magazine's common stock instead of holding a second quantity. Its stock definition must agree with the magazine. Other equipment-bound kits keep their own logistics stock. Internal logistics, ammunition servicing and cargo transport use the same resource identity across ships, while each receiving store uses its own capacity tag and resource size. Generating magazines use explicit recipes and the common processor, with separate work and operating-energy requirements.

Consumers bind `system`, `store` and `costs` by activity: `repair`, `structural`, `treat`, `stabilise`, `recover`. Each cost maps material IDs to positive quantities. An away team requires its own System-bound kit; stationary origin stores cannot supply it remotely.

Entity `docking.ports` records `id`, `controller`, `compartment`, `compatibility`, local `x_mm`, `y_mm`, `z_mm`, `heading_mdeg`, `capture_range_mm`, `capture_speed_mm_s`, `alignment_mdeg`, `mating_ticks`, `separation_ticks` and `units_per_second`. Optional flags are `auto_accept_allies`, `supply`, `emergency_release` and `emergency_damage`. Place ports outside the hull's collision geometry. Away missions use `allow_ship_transport: true` and a nonempty `docking_ports` list naming target ports; omission disables large-ship delivery. Target-authored boarding templates may also name `docking_ports`; the list opts those operations into physical ship delivery.

Commands carry the owning Controller/generation and the current inventory or docking revision in `value`. Human and AI commands share admission. Controller trees opt into `logistics_commands` or `docking_commands` and receive `logistics.revision` or `docking.revision`; unused trees retain their existing vocabulary fingerprints.

| Command                                        | `system`                   | `text`                                                       |
| ---------------------------------------------- | -------------------------- | ------------------------------------------------------------ |
| `logistics.target`                             | Store                      | `material/quantity`                                          |
| `logistics.hold`, `logistics.release`          | Main store                 | Empty                                                        |
| `logistics.cancel`                             | Request ID or `batch:<id>` | Empty                                                        |
| `logistics.route`                              | Source compartment         | `destination/open` or `destination/close`                    |
| `logistics.transfer`                           | Source store               | `entity/receiving-store/material/quantity`                   |
| `logistics.craft_allow`                        | Carried craft ID           | `1` or `0`                                                   |
| `logistics.deliver`                            | Source store               | `craft/cargo-store/entity/receiving-store/material/quantity` |
| `logistics.cargo_cancel`                       | Cargo job ID               | Empty                                                        |
| `dock.accept`, `dock.reject`                   | Port                       | Entity ID, or `0` for allied standing permission             |
| `dock.supply`                                  | Port                       | `1` or `0`                                                   |
| `dock.connect`, `dock.assist`                  | Own port                   | `entity/target-port`                                         |
| `dock.cancel`, `dock.undock`, `dock.emergency` | Own port                   | Empty                                                        |

## Content and continuation

The production Cruiser has finite main stores, local magazines, repair/medical kits, infirmary supplies and docking ports. Its utility shuttles carry cargo; docked combat-craft ammunition servicing uses the shared stock/request path. Every ammunition service connection requires compatible magazine-bound logistics stores at both endpoints; unsupported legacy links are rejected during content verification. Existing content without logistics retains its previous behaviour.

Build `development_packs`, run `native --base build/linux/test.zip`, and select **Storage and supply**. The supply carrier starts short of ammunition, repair parts and medical supplies, with an injured officer. The allied survey ship has finite replenishment stock. Dock for supply and away-party delivery, then authorise a shuttle cargo run. The original Carrier exercise also supports docking delivery.

World schema 17 retains inventories, requests, batches, reservations, fractional work, standing craft permissions, cargo jobs and docking connections. Away schema 10 retains the entry compartment. World and portable recovery require schema 17; older saves are refused with a new-session message. Restore validates a replacement before changing the live world and rebuilds radio endpoints after restoring craft activity. No old-save migration is provided.

Trade, fabrication/recycling, salvage and general displacement cargo transport remain outside this increment.

## Verification

`system.logistics`, `system.docking` and `system.cargo` load independent synthetic fixtures through production paths. They cover conservation, shared capacity, first-eligible stock allocation, deliberate route blockage, resumed transit, scoped observations, stale authority, AI admission, assisted/manual approach, docking supply, docking-only away delivery, cargo permission/commitments and exact resumed sorties. Medical integration covers finite consumables and interrupted paid work. Native/WASM continuation comparisons are included in the ordinary check scripts. Production verification checks bindings and capacities without freezing tuning.

Graphical journeys, real screen-reader use, touch/gamepad and six-display performance require separate manually invoked evidence. Compilation and headless system cases do not establish that usability evidence.

### Cleanup verification — 2026-10-22

The cleanup shares batch-return eligibility, occupied-volume accounting and pending-delivery accounting. Serviced magazines require compiled logistics-store bindings. Compartment state owns access closures independently of inventory, and docking uses the existing flight pursuit helper with its own approach speed and port waypoints.

- All 35 native system cases outside the inference-adapter suite passed, including cancellation during physical delivery, simultaneous receiving reservations, stores-free compartment access, finite ammunition servicing, moving-receiver docking and passenger recovery.
- Logistics, docking and cargo continuation traces matched between native and WASM.
- All 11 affected WASM integration cases passed: logistics, docking, cargo, medical, craft, away, combat, security, conversations, Controller runtime and motion/radar/trees. Browser client, Workshop and server builds passed.
- The native build check, production pack, development storage exercise pack and web-launcher tests passed.
- `system.inference-adapters` could not open its localhost test server: the sandbox returned `PermissionError: [Errno 1] Operation not permitted`.

Integration with the latest main branch passed the full native build, all 37 native system cases, the native build check and web-launcher tests. The inference-adapter case passed with localhost network access enabled, resolving the earlier sandbox blocker.
