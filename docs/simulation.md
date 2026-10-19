# Simulation foundation

The gameplay phase now adds instance-bound machinery alongside the unchanged
legacy adapter. Named/typed fittings compile once to independently stored typed
settings, requests, remainders and controller cursors in a Flecs component.
Explicit fitted-controller, fitted-steering and fitted-engine phases run before
integration. Stable definition ordering fixes shared-body actuator order.
Commands retain their resolved instance through admission and execution; Console
observations contain only exact permitted feeds. Schema-3 saves preserve fitted
continuations and reject malformed rosters/state transactionally. Full resource,
knowledge, richer-tree and gameplay delivery remains tracked in
[the gameplay implementation ledger](gameplay-implementation.md).

The simulation library owns Flecs components and advances explicit **20 Hz ticks**. It has no renderer, UI or browser dependencies. Explicit phases and authored stable IDs at order-sensitive boundaries define gameplay ordering; Flecs allocation and query order do not. Authoritative motion uses integer millimetres, millimetres/second and millidegree headings, integer square root and integer CORDIC directions. Sub-millimetre displacement and rate remainders survive snapshots. The world plane uses +X/right, +Y/up, heading zero along +X and increasing counterclockwise; Workshop uses the same convention.

## Data-oriented execution

`World::load` compiles verified equipment definitions into typed settings and state trees into flat node/output arrays. Runtime cursors use numeric indices; state names remain separate for save compatibility. No authored JSON lookup, string action dispatch or tree-output allocation occurs inside the simulation systems.

The operating-action descriptors in `phoenix.contracts` own command names, value types, ranges and required capabilities. Authored tree validation/compilation and external-command admission use those same definitions; author diagnostics and console ownership checks remain at their respective boundaries. Runtime tree entry is derived from elapsed ticks, while schema-1 saves retain and validate the original `entered` field.

Flecs stores separate stable identity, definition metadata, pose, motion and collider components. Only fitted entities receive engine, steering, radar and controller components. `Movable` selects finite-mass entities; `Claimed` excludes human-controlled entities from delegated work. Claim changes happen between ticks. The stable-ID map remains a command/save boundary index, rather than a simulation iteration source.

Each tick applies the admitted typed command queue in submission order, then explicitly runs six Flecs systems:

1. Controllers emit typed requests through the same capability checks as player requests.
2. Steering updates headings for movable entities with steering.
3. Engines update velocity for movable entities with engines.
4. Integration advances movable poses and integer remainders.
5. Collision processing gathers numeric data through a Flecs table iterator, sorts by stable ID, builds a bounds tree, resolves four sequential candidate-pair passes and writes positions/velocities back to ECS.
6. Radar indexing gathers the final positions through a Flecs table iterator and rebuilds a separate point-bounds tree for observation queries.

The first four systems operate independently on each entity, so table iteration order cannot affect their results. Collision scratch storage is reused between ticks; component pointers are held only during that phase, with no structural changes. Inspection uses a cached Flecs query; radar uses its spatial index. Both sort public outputs by stable ID.

### Spatial indexes

Both indexes use a flat, balanced bounding-volume hierarchy with integer bounds, median spatial splits and reusable vector storage. Construction uses O(n log n) work and O(n) storage; a changed leaf refits its ancestors in O(log n). There is no world-sized grid, cell-size tuning or expansion proportional to collider size. Circle bounds use radius; box bounds use half-width plus half-length, matching the existing conservative rejection for rotated boxes.

For each collision body in stable-ID order, the tree returns the lowest overlapping ID after the last visited pair. After resolution, both changed bounds are refitted before requesting the next candidate. Consequently, corrections that create new later contacts are seen in the same pass, while previously visited pairs are not revisited until the next pass. Skipped pairs are exactly those rejected by the original conservative bounds check; the narrow phase and integer response are unchanged. This preserves the original sequential collision behavior without enumerating every distant pair.

Radar queries the point tree with an inclusive range square, then applies the existing integer distance check to each candidate. Point bounds deliberately exclude collider extents: radar range is measured between entity positions. The index is ready immediately after load/restore and rebuilt after each tick's collision corrections. It holds stable entity handles and copied positions, never component pointers across claim changes. Indexes are derived data and are not serialized.

Sparse worlds avoid most pair and radar-distance checks. Dense or world-spanning bounds can still produce all collision pairs or all radar candidates; no universal subquadratic bound or measured frame-rate improvement is claimed.

## Runtime contracts

Explicit Stations use `(ship entity, Console id)` addresses. Claimants select a seat; an omitted seat is accepted only for a single-Console ship. Distinct Consoles on the same ship can be claimed concurrently. Command admission checks the Console's control mask and optional qualified System name, and observation exposes only its permitted System feeds. Releasing a Console cancels only that claimant's pending input, restarts only its assigned controllers and preserves other Consoles' work. Without a replacement controller, an explicitly assigned System keeps its last requested setting. Legacy release still resets desired speed/heading as before.

Worlds containing explicit Stations save schema 2, including Console addresses and separate System controller cursors. Legacy-only worlds still emit the original schema 1. Both restore transactionally. Local sessions expose independent route claims, commands and observations; portable recovery clears every live route and retains the selected local Console for explicit reclaim. Display and network integration are separate from these headless contracts.

`World::load` accepts a verified catalogue, map ID and optional named-slot/allowed-ship choices. Each chosen slot spawns a stable entity after authored instances. `claim` grants a local console control of one eligible ship; `submit` checks claim/target, capability, action and range, then queues a request for the next tick. Releasing a claim removes its pending requests and invalidates the old console. These are local admission contracts; transport authentication, claim replacement confirmation and replicated commitment are later host/session responsibilities.

Engines drive towards a desired speed along current heading with bounded acceleration. Steering drives towards a desired heading with bounded turn rate. Disabling the engine preserves inertia. Setting desired speed zero requests braking; it does not erase velocity. Radar supplies current range-filtered contacts only through the claiming console's observation path. Disabling radar removes its live contacts. This is the foundation sensor model, not the GDD's full uncertainty/history/provenance system. Engines do not yet implement the full energy/service/thermal network.

Solid circles and oriented boxes use stable pair iteration, positional separation and an inelastic normal response with finite mass or anchored zero-mass scenery. Controlled tests exercise circle/circle, circle/box and box/box response, opposing finite-mass momentum and rotated static boxes. Contact detection is currently discrete: continuous collision detection, extreme-speed tunnelling, dense-contact stability and performance certification remain pending. Coordinates/velocity components are bounded to ±1,000,000,000 to keep the integer arithmetic domain finite; reaching that implementation boundary clamps the state. No arbitrary-hardware frame-rate claim is made.

Entities can reference a reusable `controller` definition from the catalogue's `trees` array. This initial state-tree vocabulary is a bounded timed state graph: `initial`, a `states` object, and each state's positive `ticks`, named `next` transition and optional `speed_mm_s`, `heading_mdeg`, `engine_enabled` and `radar_enabled` requests. Verification checks transitions, output vocabulary and compatible fitted capabilities. Unclaimed entities run their own serialisable cursor; output requests pass the same capability admission as player requests. Hierarchical/contextual job trees, resource commitment and broader GDD work are not implemented by this small controller vocabulary.

`World::save` captures a tick boundary, content identities, stable entities, ownership, motion/remainders and tree continuation. Saving with queued requests is refused. `restore` verifies identity/roster/continuations and prepares a replacement before touching the running world. It requires the selected catalogue; session/fleet layers own portable bundles and recovery lobbies.

The **schema-1 save format is unchanged**: `body` and `operation` records retain their original fields, named tree state, ordering and defaults. These are serialization records, not runtime components. Non-default inactive operation fields accepted by older saves are retained in a cold compatibility component only when needed. Restoring claims reconstructs `Claimed` tags; recovery's `clear_claims` removes them without resetting controller work or operating requests.

The native and WASM integration case loads synthetic content through the production ZIP/JSON/PNG loader, exercises the real admission/Flecs execution/observation paths and compares resumed work with uninterrupted work. The automatic gate also compares native/WASM continuation from the same controlled run. There is no historical world hash or production ship speed golden.

The ECS refactor extends that case with mixed fitted capabilities, static controllers, fractional rates, reversed entity creation order, dense contacts, claim/release/recovery transitions and schema-1 inactive-field round trips. A temporary pre-refactor trace captured these synthetic scenarios for before/after comparison; it is not a checked-in golden or a production balance assertion.

Spatial-index checks additionally exercise correction-created contacts, radar refresh after collisions and motion, inclusive negative-coordinate range edges, sparse populations, empty/single-entity worlds, large colliders at the coordinate limit and immediate observation after restore. The indexed native/WASM traces match a temporary pre-index run of the same synthetic scenarios.
