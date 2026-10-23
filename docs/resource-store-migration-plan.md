# Resource, store and recipe migration

23 October 2026 · implementation plan; implementation and acceptance are pending.

This is the third slice of the [shared gameplay engine](gameplay-engine-design.md), following [shared property effects](property-effects-evidence.md). It unifies all existing stored resources and adds shared recipes and processors. It preserves the existing physical supply, funding, thermal, authority and knowledge rules.

## Intended result

A magazine, battery, coolant tank, team kit and cargo hold use the same resource catalogue and stock accounting. Each quantity has one authoritative owner. Physical mechanisms refer to stores rather than keeping parallel ammunition, energy or coolant counters.

A resource has a stable catalogue identity wherever it travels. A shell can occupy one unit in a magazine and several units in a cargo hold because each store chooses its capacity measure. A recipe, weapon, team or scenario operation consumes exact resource IDs through common accounting. The mechanism still decides how it obtains supplies and what its operation does.

Electrical connections remain electrical connections. Internal deliveries, docking crossings and cargo shuttles retain actual movement and commitments. Sharing storage does not make all stock universally accessible.

## Settled scope

- Include material inventories, all magazines, electrical batteries/capacitors and coolant reserves. FTL reserves and carried-craft stocks migrate with their existing owners and consumers.
- Include the mod-extensible resource and recipe catalogues and batch/continuous processor support. Existing energy-to-ammunition replenishment is the first production conversion; synthetic fixtures prove general recipe accounting.
- Keep weapons selecting separate magazines. A magazine contains one ammunition resource type. General stores can optionally enforce the same single-resource restriction; otherwise they can hold mixed resources.
- An exact-resource filter fixes that type. A tag-filtered single-resource store retains its chosen resource until deliberately changed while empty and free of commitments. Exhaustion does not cause automatic substitution.
- Include authored add, consume, set, fill and transfer operations through the existing trusted effect/scenario execution boundary, plus the existing deferred reaction integration.
- Migrate physical consumers, Controller/Console projections, player controls, Workshop, maintained content and persistence together.

New fabrication/recycling gameplay, trade, salvage, cannibalisation, displacement cargo transport and campaign production are deferred. General directed-system and weapon-payload effect conversion remain the following stage. This slice does not turn every generator, engine, cooling process or finite job into a recipe.

## Existing-code inventory

The inventory is based on current V3 code, including ongoing rescue/docking work in the shared checkout. Recheck those boundaries when implementation begins; preserve unrelated changes.

| Existing area                          | Current representation                                                                                           | Migration responsibility                                                                                                                                                          |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Resource catalogue                     | Material IDs and one `volume` are defined separately inside each entity's `logistics`                            | Global resource definitions, tags, per-tag sizes, shared references and mod overrides; reject conflicting legacy identities during conversion.                                    |
| Main holds, local stores and team kits | `MaterialInventory` and `MaterialStoreState` in `simulation/logistics_state.hpp`                                 | Shared store quantities, filters, capacity and stable identities. Keep targets, holds, actual location and local ownership.                                                       |
| Magazines                              | `MagazineState.ammo`, `reserved_return` and `MagazineDefinition`; logistics special-cases magazine-backed stores | One shared quantity owner, persistent single-resource selection, and explicit receiving-space commitments for loaded rounds. Remove the special stock-access branch.              |
| Weapon loading and firing              | `combat_weapons.hpp` tracks selected/loaded magazines, load/unload timers and ammunition metadata                | Retain magazine selection and native firing. Capture the actual loaded resource and quantity; use common stock/payment operations without changing an already loaded payload.     |
| Electrical reserves                    | `ResourceNetwork` owns `ResourceStoreState.energy` alongside heat, stress, flow and fractional work              | Shared stock ownership; keep allocation, budgets, cut/restore policy, rate limits, heat and operating credits in native electrical state.                                         |
| Coolant                                | `CoolantSourceState.remaining` and cooling-link allocations                                                      | Shared coolant store; keep cooling reservations, actual-removal consumption and fractional accounting. Add explicit authored tank capacity where only initial stock exists today. |
| Replenishment                          | Magazine `none`, `energy` and `infinite` reload strategies; milli-tick refill work                               | Physical supply remains logistics. Convert generating strategies to recipe/processor definitions and state; preserve applicable reload modifiers and funded progress.             |
| Repair and Medical                     | Material consumer costs, `pay_material_work`, structural work credit, team/facility progress                     | Consume shared resource IDs; preserve paid commitments, interrupted work, carried-kit access and actual team location.                                                            |
| Deliveries and cargo                   | Requests, batches, receiving reservations and cargo jobs across `logistics.hpp`, `docking.hpp`, `cargo.hpp`      | Shared stock/capacity accounting with existing first-eligible allocation, physical crossings and cancellation/return rules.                                                       |
| Craft servicing and reporting          | Energy transfers through the resource network; ammunition through magazine stores                                | Bind both to shared stores. Preserve finite servicing, throughput, permissions, cargo/passenger/sortie commitments and delayed reports.                                           |
| FTL and shields                        | FTL uses a network reserve; shields consume energy and discharge into a named reserve                            | Migrate store bindings and accounting, retaining readiness, losses, thermal state, conversion rules and prepared reaction payment. Shield strength stays shield state.            |
| Other powered mechanisms               | Propulsion, sensing, weapons, Comms, psychic equipment and transport consume native resource grants              | Preserve grants and operation semantics while their backing quantities move. Do not convert their domain behavior as incidental scope.                                            |
| Content, tools and continuation        | Catalogue/compiler, Workshop/export closure, projections, world/session serializers and tests                    | Add both definition kinds, compile all references, remove retired authoring paths and validate complete saved commitments.                                                        |

Relevant starting points are `src/resources/`, `src/content/logistics.cpp`, `src/content/resource_bindings.cpp`, `src/content/combat.cpp`, `src/content/workshop.cpp`, `src/simulation/gameplay_resources.*`, `src/simulation/logistics*`, `src/simulation/combat*`, and the craft, docking, cargo, FTL, shield and team consumers.

## Resource and store contract

### Definitions and identity

Resources are static catalogue definitions with stable IDs, localisable labels, tags, positive sizes for supported capacity tags, and optional effect references. Shared definitions use the existing dependency, inheritance, explicit override and export-closure rules. Compilation resolves names into typed runtime references; no per-resource C++ enum or per-tick JSON interpretation is introduced.

Keep the current integer quantity units for migrated resources. Use checked integer/fixed-point accounting for rates and fractional work. Define representable bounds and validate products, sums and conversions before mutation. A displayed unit is distinct from the store's capacity measure.

Move current ammunition metadata to a shared resource-associated native ammunition profile sufficient to preserve existing projectile, loading and cycle behavior. Weapons retain their own range, targeting, modes and supported defaults. This is catalogue extraction and binding, not the general payload-effect overhaul. Resource effect references are inert unless a consuming mechanism explicitly invokes them; storing or transferring ammunition cannot apply its payload.

Resolve repeated legacy material/ammunition names deliberately. Matching names with incompatible size or ammunition definitions must be split into explicit IDs or reconciled as an authored decision, never merged by whichever ship loads first.

### Capacity and single-resource selection

Each store has a stable entity/store identity, an owner/location binding, acceptance rules, one capacity tag, capacity and actual quantities. Native equipment and networks refer to that store. Heat, stress, charge/discharge budgets and processor progress are separate state, not extra stock owners.

Acceptance supports tag and exact-ID filters with the already agreed AND/OR composition. An accepted resource must provide a positive size for the store's capacity tag. Mixed contents share one capacity:

```text
occupied capacity = sum(quantity × size under the capacity tag)
available capacity = capacity − occupied capacity − committed receiving space
```

An optional single-resource restriction is independent of the acceptance filter. Magazines enable it. A specific-resource magazine has a fixed resource. A tag-filtered magazine has an authored initial selection or an explicit permitted selection before use. Other compatible types remain unavailable until the selection changes.

Changing that selection requires zero contents and no loaded-round returns, dispatched deliveries, cargo commitments, processor output reservations or other active claims. Cancel obsolete waiting replenishment requests and update targets as part of the admitted change; waiting work is not a reason to steal or discard committed stock. Save the chosen type even when the magazine is empty. Weapons continue to select magazines rather than silently switching ammunition resources.

Store placement follows the actual owning mechanism. A deployed team's kit follows that team and cannot draw remotely from its origin hold. A carried craft owns its own stores. Compartments, access routes and structural disablement retain their existing meanings.

Dynamic changes to capacity, filters or capacity tags through property effects are deferred. Static catalogue overrides apply when content is loaded; they do not resize a running store.

### Quantities, commitments and execution

Separate actual stock, claims on stock, receiving-space reservations, in-transit quantities and completed consumption. Every commitment has a stable owner/purpose and can be validated and released once. Do not count a loaded round or dispatched batch in both the source store and its new location.

Use shared prepare/validate/commit operations for stock changes and capacity claims. A finite indivisible step secures all its required resources and outputs atomically. Costs must be secured from available stock before the step's outputs; a step cannot pay its own entry cost using an outcome it has not produced. Multiple references to the same store share the prepared accounting, rather than each validating against the same untouched balance. A failed attempt leaves no new consumption or reservations. Separately authored standby or continuous operating costs retain their own meaning.

Maintain existing physical rules:

- Main-hold allocation remains first eligible authorised request with stable ordering. Queuing alone reserves neither stock nor destination space.
- Dispatch moves stock into a real batch and reserves receiving space atomically. Arrival deposits once. Blockage, cancellation and return preserve location and quantity.
- Electrical allocation keeps its native budgets, finite links, cut/restore policy, charge/discharge limits, losses, thermal gates and fractional work credits. The network operates on shared stores, not a synchronized shadow balance.
- Cooling spends coolant for actual heat removed. Reserved cooling capacity and coolant quantity remain different concepts.
- Loading removes ammunition into an explicit loaded commitment and reserves return space. Firing consumes that commitment; unloading returns the same resource through the reservation. Selection changes cannot transmute a loaded round.
- Already paid repair, clinical and processor work survives ordinary interruption without paying again. Irreversible completed consumption is not refunded by reconstructing a definition or restoring a save.

Transport permission comes from the mechanism and connection, not merely a resource's tags. A cargo filter does not grant electrical connectivity, and a common store interface does not bypass a bulkhead, dock permission or delivery journey.

### Authored store operations

Add, consume, set and fill address an explicit store and resource. Fill uses currently uncommitted compatible capacity. Set changes that resource's amount without replacing unrelated contents. Transfer names source and destination and conserves quantity. Finite operations request an exact amount and fail atomically when it cannot be accepted; flow adapters may explicitly quote a bounded partial amount before committing it.

All operations respect compatibility, single-resource selection, capacity and existing commitments. Ordinary fill/set operations do not silently cancel someone else's job or destroy stock to make room. Intentional creation/removal is distinct from a conserving transfer. Unsupported destructive changes to commitments require a separate explicit operation rather than an implicit overflow rule.

Reuse prepared effect execution and stable receipts for scenario/tree and deferred reaction operations, including mixed groups of store operations and existing effects. A scenario can directly fill a battery without a player identity or fictitious supply journey. This authority does not expose hidden stores to ordinary Controller commands or observations.

## Recipes and processors

Recipes are shared tagged definitions with exact resource inputs, exact resource outputs and required work. Ingredient tags do not substitute resources. A processor selects supported recipe IDs/tags and binds their inputs and outputs to explicit compatible stores. Recipe selection must respect each store's chosen resource and cannot silently change a magazine's type. Its availability, work rate, operating costs and controls belong to the fitted mechanism.

Support batch and continuous processing through common accounting:

- A batch prepares its input set and required output space before consuming inputs and beginning work. Consumed inputs and paid progress remain committed while paused. Completion publishes all outputs once.
- Continuous processing advances only the amount of work supported by its input quantities, required output capacity and funding. Fixed-point remainders persist; neither rounding nor a full output store creates free output or erases paid work.
- Required outputs cannot be discarded to keep processing. Output blockage pauses the relevant work and prevents new operating payment for that work; separately declared standby costs remain possible.
- Deliberately changing recipe abandons unfinished work without refunding consumed inputs. Retain compatible unused inputs and completed products within their stores. Release only the abandoned operation's unfulfilled claims. Normal logistics performs any subsequent movement.

Migrate existing energy-generated ammunition as a batch output with its native operating energy rate and reload work modifier. Explicit free/infinite replenishment becomes an explicitly input-free, timed recipe; it does not make other stores infinite. Keep recipe input costs distinct from per-second operating costs so modifiers do not accidentally charge either twice. General processors use the shared property mechanism for their supported work rate; migrated magazine reload effects reach that work rate once.

The current magazine refill code clips a completed batch to remaining capacity. The recipe contract replaces that behavior: a full required batch must fit, including return/delivery reservations. Audit migrated batch sizes and content for this intentional difference. Do not silently change batch size, cost or cycle time to hide it. A partial top-up recipe can be authored explicitly where wanted.

General fabrication and recycling need no production content in this slice. Synthetic batch and continuous recipes prove multiple exact inputs/outputs, blockage, shortage, switching and continuation through the real processor implementation. Generation response, thermal physics, coolant recovery loops and bespoke team jobs retain native ownership unless explicitly converted in a later stage.

## Implementation sequence

### 1. Compile shared resources and recipes

Add catalogue definition kinds, inheritance/overrides, typed compilation, native ammunition profiles and transitive reference closure. Inventory and map current material/ammunition identities. Add Workshop definition selection and basic editing alongside the schema, including tags, sizes, filters and single-resource selection.

**Gate:** synthetic packs resolve references and mod overrides, reject conflicts and missing sizes, and preserve resources, recipes and effect references through draft/export/reload. Maintainable content has an explicit conversion map; catalogue types alone do not establish migration.

### 2. Deliver shared stores through materials and magazines

Implement one quantity/capacity/commitment owner. Convert material inventories and magazine stock together, including loaded-round commitments, return space, local targets and the single-resource lock. Keep native loading/firing behavior and magazine selection. Connect internal dispatch and arrival to the same accounting rather than maintaining a compatibility balance.

**Gate:** shared capacity with different per-tag sizes, one-type magazine refusal/selection, finite loading/firing/unloading, concurrent delivery reservations, first-eligible shortage, blocked transit and save/restore all work through production paths. No shadow magazine quantity remains.

### 3. Move electrical and coolant reserves

Bind the electrical solver and cooling providers to shared stores. Keep native heat/stress, rates, thermal stops, supply grants and fixed-point credits. Convert every energy consumer, including FTL, shields/reactions, transport and craft servicing, and add explicit coolant tank capacity in authored content.

**Gate:** generation, flow, loss, spending and cooling conserve or deliberately transform quantities under their existing rules; headroom includes shared reservations; disabled/unfunded operation remains blocked. FTL and shield discharge see the same balances as store observations. No shadow energy or coolant quantity remains.

### 4. Complete delivery and finite-work consumers

Convert docking supply, cargo shuttles, carried-craft ammunition servicing, team kits, facilities, repair, Medical and structural work credit. Reconcile all destination-space and source-stock claims with the shared accounting. Keep standing permissions, crossing work, physical returns and delayed craft reports.

**Gate:** a cross-ship delivery conserves resource identity and quantity through holds, docking and a shuttle; interruption cannot duplicate/refund stock. Deployed teams use carried supplies, and paid work resumes once. Human/AI admission and scoped information remain equivalent.

### 5. Deliver processors and migrate replenishment

Add recipe selection, batch/continuous state, native work/funding integration, outputs and switching. Replace energy/free magazine refill branches with processors. Retain existing property-effect bindings and review the changed full-output requirement against maintained content.

**Gate:** a real funded recipe replenishes a magazine; mixed input requirements commit atomically; full outputs, missing inputs and zero work rate stop new work/payment; switching and save/restore preserve conservation and completed products. Batch and continuous fixtures demonstrate exact native/WASM arithmetic.

### 6. Expose store operations and finish authoring/presentation

Add supported store-operation effects and trusted tree actions through shared preparation. Finish standard store/processor projections, permitted controls, failure reasons, Controller bindings and Workshop previews/verification. Migrate production/development definitions and synthetic fixtures; remove retired entity-local material definitions and duplicate storage/reload paths.

**Gate:** an objective fills a battery, a transfer conserves stock, and a deferred paid reaction commits a mixed operation group or nothing. Hidden stocks remain hidden. Exported content reloads with complete dependencies. Search/inspection finds no active legacy quantity owner or silent fallback schema.

### 7. Validate continuation and review integration

Use stable logical resource/store/recipe IDs in persisted state and validate every quantity, selected type, reservation, loaded commitment, in-transit batch and processor work record before replacing the live world. Rebuild derived lookups without applying initial stock or replaying completed effects. Select the next world/session schema at implementation time; reject unsupported older saves consistently.

Run an independent agent review focused on conservation, payment ordering, capacity claims, single-resource changes, stale prepared operations, information boundaries and missing consumers. Fix findings, run affected cases, then the complete native and WebAssembly gates. Commit verified logical steps; record actual failures/retries separately from passes.

**Gate:** native/WASM continuation matches through consequential loaded-round, delivery, thermal/fractional funding, processor and pending-effect boundaries. All migrated content verifies. There is one authoritative stock path, with documented specialist state owners and no unresolved review findings.

## Validation and delivery boundaries

Follow [testing policy](testing.md). Extend the existing resource/gameplay, content, combat, logistics, docking, cargo, Medical, craft, FTL, scenario and Controller integration suites with synthetic production-loader fixtures. Add a focused processor suite if its new contract warrants one. Do not derive fixtures or exact balance assertions from shipped ships.

Concentrate on conservation, mixed capacity, exact resource identity, single-resource locking, atomic payment/output claims, no double-counted loaded/in-transit stock, full-output blockage, interrupted paid work, malformed restore rejection and permitted observations. Ordinary mod verification checks valid resolved content without freezing tuning. Include new catalogue definitions in Workshop dependency export and native/browser pack verification.

Player store/processor controls retain localisation, keyboard, touch, gamepad and semantic accessibility paths. Graphical journeys, physical devices, screen readers and performance remain separately triggered smoke evidence. A headless pass does not establish those acceptance requirements.

Exact field names, compiled container layout, bounded counts and phase placement are implementation details to settle in the first step against these contracts. Changes to resource identity, single-type magazine behavior, allocation, conservation or recipe accounting are design changes and must not be hidden in that work.

## Completion

- [x] Every existing material, ammunition, electrical and coolant stock uses shared store ownership.
- [x] Resource/recipe definitions, mod overrides and export closure work through the catalogue.
- [x] Magazines retain one chosen ammunition type and loaded/return commitments preserve its identity.
- [x] Electrical flow, cooling, deliveries and finite work preserve their native operating contracts.
- [x] Recipes/processors replace generating reload paths, with no discarded required output or duplicate payment.
- [x] Trusted store operations, UI/Controller projections, Workshop and maintained content use the same model.
- [x] Legacy quantity/authoring paths are removed and continuation/review evidence is recorded.
- [x] Manual acceptance and deferred gameplay are listed separately.

Completed 24 October 2026. See the [implementation record](resource-store-evidence.md) for the supported continuous-recipe ratio bound, schema 22 migration, independent review and actual native/WebAssembly validation results.

The recommended following stage remains directed systems and weapon payload effects. This plan does not establish their implementation or approval.
