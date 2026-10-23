# Simulation

The live runtime follows the [Console Controller architecture](controller-architecture.md). The [refactoring plan](controller-refactor-plan.md) records verified migration steps and remaining integration evidence.

The [shield migration](shield-migration-evidence.md) moves shield reserves and transitions to a ship-owned component. Equipment supplies operating, funding and control bindings; authored scenario grants use the same native component. Due grants expire before scenario evaluation. Contribution damage hooks run inside native absorption; a world-owned queue executes due secondary reactions before ordinary ordnance and destruction processing, with newly generated work deferred to a later tick. Controller observation snapshots retain their existing boundary.

## Entities, equipment and Controllers

Shared property effects supply typed additive values to native mechanisms. Independent applications expire before operation; officer contributions are derived from current assignment, presence, health and deployment facts. Scenario and deferred reaction applications use the shared effect preparation path. A modifier survives loss of its source, while removal of its actual shield target reconciles attached contributions before the tick completes. World/session schema 19 persists independent applications and milli-tick magazine progress. See the [implementation record](property-effects-evidence.md) for boundaries and evidence.

Flecs owns physical entities with stable identity, pose, motion and collider components. Fitted Systems are child entities with applicable engine, steering, radar, paid-flight and thermal state. Their parent relationship is authoritative. `EquipmentIndex` is a derived ordered handle lookup rebuilt on load/restore; it neither duplicates ownership nor drives global simulation iteration. Equipment handles stay private and do not create scene bodies or radar contacts.

Console-owned Controllers hold command sections, operator generations, AI trees/private memory and effect receipts. They do not mutate physical components. Human and AI commands pass the same admission and tick-boundary commit, then physical subscribers consume changed settings or single-executor requests. Resource networks remain entity-owned typed arrays. Physical Systems and reporters continue operating regardless of human or AI control.

`World::load` compiles verified definitions into typed layouts and programs. Legacy definitions convert once when ownership is unambiguous; System-context policies and retired timed trees require explicit migration. There is no legacy live AI dispatch. Names/JSON belong at compilation, transport and persistence boundaries. A Console's profile and operator availability decide who writes each Controller section; no ship-wide player/AI mode selects a separate simulation.

## Fixed-tick data flow

Simulation advances explicit **20 Hz ticks**, with no renderer or browser dependency. Integer millimetres, millimetres/second and millidegree headings, integer square root/CORDIC directions and saved fractional remainders support deterministic continuation. The world uses +X/right, +Y/up, heading zero along +X, increasing counterclockwise.

1. Commit admitted profile/Away changes and addressed human proposals, refusing obsolete operator generations.
2. Evaluate scenario snapshots, construct due spawns and publish scenario outputs.
3. Capture sensor positions, permitted Console/entity boards, inboxes, accepted commands and movement starts. Compute the ship's close-in lookout once from world geometry/motion and publish its navigation assessment before Controller evaluation.
4. Evaluate Console Controllers against frozen inputs and consume their committed commands; execute basic vector steering/engines.
5. In stable entity-ID and fitted-System order, quote paid-flight/passive resource demand, resolve each resource network once, and execute physical actuators with paid grants. Sample each mechanism's telemetry after its actuation.
6. Integrate poses, resolve collisions and rebuild the radar index.
7. Evaluate scenario motion/objectives, publish results, advance the clock and deliver knowledge/Coordination work.

[Material logistics and docking](storage-logistics.md) add ordered internal/external batch work, cargo commitments and a rigid docking relationship. Controller snapshots include permitted stock and port revisions. Material dispatch precedes clinical/repair work; docking capture/separation follows collision resolution. Propulsion is inhibited for attached bodies, and the receiving root carries assembly mass and collision impulses.

Paid resource execution prepares a single `ShipResourceAllocation` after all demands, including boost/impulse target reservations and FTL, are known. The resource module owns preview, unavailable-supply overrides and preservation of requested settings. Modifiers read candidate grants, then the world commits the same allocation before physical actuation. The solver advances once; neither preview nor a discarded candidate debits live stock. Allocation state is transient, while its committed quantities, grants and remainders use the existing continuation format.

Shared-body writes remain serial and independent of Flecs table/allocation order. Request identities and effect receipts prevent replay. Accepted settings survive operator changes; held manual inputs release safely. Sensor/telemetry producers supply a common permitted observation boundary for human GUI and Controller decisions. AI decisions cannot read hidden components or fabricate producer facts. The close-in lookout is a ship-owned physical producer with direct world access; Controllers read only its shared blackboard assessment.

### Spatial indexes

Both indexes use a flat, balanced bounding-volume hierarchy with integer bounds, median spatial splits and reusable vector storage. Construction uses O(n log n) work and O(n) storage; a changed leaf refits its ancestors in O(log n). There is no world-sized grid, cell-size tuning or expansion proportional to collider size. Circle bounds use radius; box bounds use half-width plus half-length, matching the existing conservative rejection for rotated boxes.

For each collision body in stable-ID order, the tree returns the lowest overlapping ID after the last visited pair. After resolution, both changed bounds are refitted before requesting the next candidate. Consequently, corrections that create new later contacts are seen in the same pass, while previously visited pairs are not revisited until the next pass. Skipped pairs are exactly those rejected by the original conservative bounds check; the narrow phase and integer response are unchanged. This preserves the original sequential collision behavior without enumerating every distant pair.

Radar queries the point tree with an inclusive range square, then applies the existing integer distance check to each candidate. Point bounds deliberately exclude collider extents: radar range is measured between entity positions. The index is ready immediately after load/restore and rebuilt after each tick's collision corrections. It holds stable entity handles and copied positions, never component pointers across claim changes. Indexes are derived data and are not serialized.

Sparse worlds avoid most pair and radar-distance checks. Dense or world-spanning bounds can still produce all collision pairs or all radar candidates; no universal subquadratic bound or measured frame-rate improvement is claimed.

## Admission and continuation

Claims address an entity and Console; command envelopes add Controller, channel, operator generation and request identity where applicable. Each physical input has one command source, while one setting may feed multiple compatible mechanisms. Human access requires a current claim and human allocation. Fully AI Consoles and entities use the same pipeline. Publications and messages retain role/scope validation independently of command ownership.

Vector engines accelerate/brake toward requested speed and preserve inertia when disabled. Paid-flight mechanisms retain resource quotation, funding and thermal execution. Destination, follow and Match Speed use the same addressed commands as ordinary controls. Radar exposes only permitted in-range contacts; this does not implement the GDD's full future sensor estimation design. Collision and spatial behavior remain independent of Controller operation.

World and portable session saves use schema 16, with supported recovery from schemas 13–15. They capture physical state, Controller command sections/continuations, profiles, scoped knowledge, reporters and scenario state at a tick boundary. Queued commands must execute before saving. Restore prepares and validates a replacement before touching the live world, rebuilds Flecs children/indexes and verifies effective operator allocation. Old authority save schemas/fields are refused; content conversion does not imply continuation conversion. Wire envelope schema 5 refuses incompatible peers before play. Portable recovery discards live device claims and requires fresh admission.

## Verification

Synthetic production-loader journeys cover shared/split commands, human/AI parity, paid resources, hidden observations, reporting, handover, request deduplication, transactional refusal and exact resumed continuation. They do not freeze shipped tuning. The refactor plan contains current receipts; earlier ECS/spatial-index native/WASM receipts describe those earlier revisions and do not establish completion of the Controller migration's final gate. Physical devices, screen readers and six-display performance need their own manual evidence.
