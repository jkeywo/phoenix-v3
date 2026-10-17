# Simulation foundation

The simulation library owns Flecs components and advances explicit **20 Hz ticks**. It has no renderer, UI or browser dependencies. Explicit phases and authored stable IDs at order-sensitive boundaries define gameplay ordering; Flecs allocation and query order do not. Authoritative motion uses integer millimetres, millimetres/second and millidegree headings, integer square root and integer CORDIC directions. Sub-millimetre displacement and rate remainders survive snapshots. The world plane uses +X/right, +Y/up, heading zero along +X and increasing counterclockwise; Workshop uses the same convention.

## Data-oriented execution

`World::load` compiles verified equipment definitions into typed settings and state trees into flat node/output arrays. Runtime cursors use numeric indices; state names remain separate for save compatibility. No authored JSON lookup, string action dispatch or tree-output allocation occurs inside the simulation systems.

Flecs stores separate stable identity, definition metadata, pose, motion and collider components. Only fitted entities receive engine, steering, radar and controller components. `Movable` selects finite-mass entities; `Claimed` excludes human-controlled entities from delegated work. Claim changes happen between ticks. The stable-ID map remains a command/save boundary index, rather than a simulation iteration source.

Each tick applies the admitted typed command queue in submission order, then explicitly runs five Flecs systems:

1. Controllers emit typed requests through the same capability checks as player requests.
2. Steering updates headings for movable entities with steering.
3. Engines update velocity for movable entities with engines.
4. Integration advances movable poses and integer remainders.
5. Collision processing gathers numeric data through a Flecs table iterator, sorts by stable ID, resolves four sequential pair passes and writes positions/velocities back to ECS.

The first four systems operate independently on each entity, so table iteration order cannot affect their results. Collision scratch storage is reused between ticks; component pointers are held only during that phase, with no structural changes. The pair algorithm is still **O(n²)**. A spatial broad phase remains future work: it must account for positional corrections creating new later contacts within a pass while preserving the existing pair order. Radar and inspection use cached Flecs queries and sort their public outputs by stable ID; radar remains a linear scan per observation. No speedup or performance target is claimed without measurement.

## Runtime contracts

`World::load` accepts a verified catalogue, map ID and optional named-slot/allowed-ship choices. Each chosen slot spawns a stable entity after authored instances. `claim` grants a local console control of one eligible ship; `submit` checks claim/target, capability, action and range, then queues a request for the next tick. Releasing a claim removes its pending requests and invalidates the old console. These are local admission contracts; transport authentication, claim replacement confirmation and replicated commitment are later host/session responsibilities.

Engines drive towards a desired speed along current heading with bounded acceleration. Steering drives towards a desired heading with bounded turn rate. Disabling the engine preserves inertia. Setting desired speed zero requests braking; it does not erase velocity. Radar supplies current range-filtered contacts only through the claiming console's observation path. Disabling radar removes its live contacts. This is the foundation sensor model, not the GDD's full uncertainty/history/provenance system. Engines do not yet implement the full energy/service/thermal network.

Solid circles and oriented boxes use stable pair iteration, positional separation and an inelastic normal response with finite mass or anchored zero-mass scenery. Controlled tests exercise circle/circle, circle/box and box/box response, opposing finite-mass momentum and rotated static boxes. Contact detection is currently discrete: continuous collision detection, extreme-speed tunnelling, dense-contact stability and performance certification remain pending. Coordinates/velocity components are bounded to ±1,000,000,000 to keep the integer arithmetic domain finite; reaching that implementation boundary clamps the state. No arbitrary-hardware frame-rate claim is made.

Entities can reference a reusable `controller` definition from the catalogue's `trees` array. This initial state-tree vocabulary is a bounded timed state graph: `initial`, a `states` object, and each state's positive `ticks`, named `next` transition and optional `speed_mm_s`, `heading_mdeg`, `engine_enabled` and `radar_enabled` requests. Verification checks transitions, output vocabulary and compatible fitted capabilities. Unclaimed entities run their own serialisable cursor; output requests pass the same capability admission as player requests. Hierarchical/contextual job trees, resource commitment and broader GDD work are not implemented by this small controller vocabulary.

`World::save` captures a tick boundary, content identities, stable entities, ownership, motion/remainders and tree continuation. Saving with queued requests is refused. `restore` verifies identity/roster/continuations and prepares a replacement before touching the running world. It requires the selected catalogue; session/fleet layers own portable bundles and recovery lobbies.

The **schema-1 save format is unchanged**: `body` and `operation` records retain their original fields, named tree state, ordering and defaults. These are serialization records, not runtime components. Non-default inactive operation fields accepted by older saves are retained in a cold compatibility component only when needed. Restoring claims reconstructs `Claimed` tags; recovery's `clear_claims` removes them without resetting controller work or operating requests.

The native and WASM integration case loads synthetic content through the production ZIP/JSON/PNG loader, exercises the real admission/Flecs execution/observation paths and compares resumed work with uninterrupted work. The automatic gate also compares native/WASM continuation from the same controlled run. There is no historical world hash or production ship speed golden.

The ECS refactor extends that case with mixed fitted capabilities, static controllers, fractional rates, reversed entity creation order, dense contacts, claim/release/recovery transitions and schema-1 inactive-field round trips. A temporary pre-refactor trace captured these synthetic scenarios for before/after comparison; it is not a checked-in golden or a production balance assertion.
