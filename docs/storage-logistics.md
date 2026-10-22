# Storage and logistics

The implementation follows GDD sections 9.3–9.4 and 11. Routine supplies use finite aggregate quantities. Material names are authored identities, not an engine enum.

## Inventory and internal delivery

Entity `logistics` defines `controller`, `materials` (`id`, positive `volume`), `units_per_second`, `concurrency`, `batch_size`, `stores` and optional `consumers`. Stores supply `id`, `compartment`, `controller`, `capacity`, `accepts`, optional `initial`, `targets`, `main` and `system`. Capacity is shared volume. A magazine-bound store uses the magazine's own stock and capacity; it does not duplicate ammunition. Its reload strategy must be `none`.

Main stores satisfy local targets automatically. First eligible requests receive stock in creation order. Dispatch removes actual stock and reserves receiving space; waiting requests reserve neither. Batches route through compartment connections, retaining their location and remaining edge work when blocked. Handling throughput and concurrency are finite. Deliberate closures require an explicit reopening command.

Consumers bind `system`, `store` and `costs` by activity: `repair`, `structural`, `treat`, `stabilise`, `recover`. Each cost maps material IDs to positive quantities. Component and clinical work pays when progress begins; retained progress prevents double payment after interruption. Structural costs are per HP, with saved prepaid fractional work. An away team requires its own System-bound kit to consume supplies from its origin inventory while deployed.

Commands use the owning Controller and generation, inventory revision in `value`, and store ID in `system`: `logistics.target` takes `material/quantity`; `logistics.hold` and `logistics.release` control main stores. Engineering `logistics.cancel` addresses a waiting request ID or `batch:<id>`; a batch returns physically when compatible return space can be reserved. `logistics.route` uses source compartment in `system` and `destination/open|close` in `text`.

Engineering sees main and local stocks. Local operators see their own stocks. Network projections bound and allowlist material fields. World schema 16 retains inventories, batches, reservations, ordering, handling remainders and paid structural work. Older content without logistics retains its existing behaviour and receives no invented stock.

## Verification

`system.logistics` uses independent synthetic content through production loading and execution. It exercises aggregate conservation, receiving capacity, exact resumed delivery, scoped stock projections and transactional malformed-save refusal. `system.medical` includes finite consumable treatment and interrupted paid work. Production tuning is not frozen by these fixtures.

Docking, external supply, cargo-shuttle dispatch, Workshop controls and the player logistics panel are subsequent implementation stages in the approved plan; this document records completed behaviour as those stages land.
