# First combat stage

**Asteroid duel** (`base.asteroid-duel`) pits a two-player armed Courier against a larger, slower AI pirate raider with heavier guns. Helm has an accompanying human-seeking Tactical Console; Support manages structural damage and repair teams. Absent operators use the ordinary Controller runtime.

## Playing

Select a contact on Tactical, then activate individual weapons. Changing selection leaves existing assignments alone. Cancel stops the selected weapon. A turret shares one target: assigning a hosted weapon retargets the turret and its active weapons; inactive weapons remain inactive. Fixed guns retain separate assignments. Aiming and lead are automatic and use permitted observations.

Helm's optional fire-arc overlay shows dashed turret traversal envelopes and solid current weapon arcs. `fire_arcs: true` enables the radar visibility toggle; other radars default to no overlay. Accessible readouts give arc and bearing without exposing enemy equipment.

Support selects a compartment on the deck map and assigns teams. Buttons and equivalent readouts show HP, selection and structural disablement. Teams physically follow authored connections, then repair over time. Reassignment completes the current connection before routing from its endpoint. Unreachable work waits; completed teams stay in place and resume after new damage.

## Authoring

A ship's `combat` object supplies weighted `decks` with rectangular `compartments`, `connections`, `repair_console`, collision tuning and `destruction`. Compartments have stable IDs, local millimetre geometry, `hp`, optional `initial_hp`, `resistance_per_m` and `recovery_hp`. Same-deck interiors cannot overlap. Undirected connections name compartments and `travel_ticks`; cross-deck links use the same contract.

Every System on a combat ship specifies its physical `compartment`, independently of Console ownership.

| Capability    | Principal fields                                                                                                                          |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `weapon`      | `magazine`, optional `turret`, local position/bearing, `arc_mdeg`, `fire_ticks`, `ammo_use`, `projectile`                                 |
| `turret`      | Local position/rest bearing, traversal `arc_mdeg`, `traverse_mdeg_s`, hull attachment `marker`, visual entity, `scale_permille`, `flip_y` |
| `magazine`    | `capacity`, optional `initial`, `reload.strategy`, `reload.ticks`, `reload.amount`, optional reload `energy_per_second`                   |
| `repair_team` | Initial compartment, `hp_per_second`, `stack_penalty_permille`                                                                            |

Projectile definitions specify a visual entity, speed, lifetime, penetration budget, damage payload, damage deposited per metre and optional angular spread. Weapon instances may share magazines and turrets. Equipment templates supply these through normal composition. Turret orientation remains a separate capability for future hosted equipment.

Reload strategies are `none`, `infinite`, and `energy`. Below capacity, magazines perform timed refill work and add the configured amount, capped by capacity. Energy reloads require a bound store and advance during fully funded ticks, retaining progress when unfunded. Stock-fed replenishment is deferred. Weapons can use `discrete_energy` and `power_store`; only eligible launches request shot energy. Shared magazines reserve ammunition in stable weapon order.

Controller settings `weapon_target` and `repair_compartment` use normal command admission and operator generations. Target zero cancels. Repair zero clears the assignment; positive values select the one-based compiled compartment index. Controllers can use authored trees or `policy: "combat"` with `fallback: "hold"`. That policy selects visible ship contacts, uses owned flight/weapon channels and assigns repairs from its permitted structural map.

Workshop exposes combat Systems, deck geometry and a diagram, connections, destruction, Controller policy and radar options through existing Undo/Redo, Verify and isolated Test workflows.

## Physical rules

Projectiles are Flecs entities with captured launch payloads and deterministic 20 Hz motion. Swept spatial queries find moving ships and solid asteroids. Decorative asteroids have no physical effect. Ship impacts select a weighted deck independently of visual height and trace the incoming line through its compartments. Material distance consumes penetration, with resistance declining as structure is damaged.

Damage deposition spends the payload once. An exiting shot retains unused payload; a stopped shot deposits its remainder at the stopping compartment. A line parallel to a shared compartment boundary is not counted twice. The Courier's forward cannon is tuned for high penetration and low damage: a typical transverse crossing deposits roughly half its payload, whereas a longer longitudinal crossing is more likely to deposit it all. Shipped tuning is not frozen by tests.

Zero compartment HP disables housed machinery. Repairs clear structural disablement at the recovery threshold; it returns only at zero. Accepted machinery orders survive the interruption. Teams represent mobile personnel and can leave their damaged starting compartment; destruction ends all work.

Teams contribute in descending repair-rate order with stable-ID ties. Rank `r` contributes `rate / (1 + k*r)`, where `k = stack_penalty_permille / 1000` defaults to 0.5. Saved integer remainders preserve work. Repair supplies are unlimited.

Closing collision energy and authored threshold/scale damage contact-facing compartments on ships. Stationary contact does not farm damage. Asteroids provide cover and collision hazards; asteroid destruction is deferred.

Zero remaining structural HP everywhere irreversibly destroys the ship. This revises the earlier GDD catastrophe-only rule. Ship-authored `destruction` chooses a visual entity, WAV `sound`, `growth_ticks`, `fade_ticks` and `size_mm`. A single-frame billboard grows; the ship visual disappears after growth; the billboard fades. The ship presentation plays the sound once and does not replay restored effects. Physical loss preserves session hosts and Console claims. Maps with `combat_duel: true` report Victory, Defeat or simultaneous Draw, ending further combat and repairs.

The pirate's top turret traverses 270 degrees centred forward; the bottom traverses 180 degrees centred forward. Turret sprites independently select among eight camera-relative bearings with the same five-degree hysteresis as ships. History belongs to each mount and view. Hysteresis does not quantise physical aim or radar arcs.

## Verification and remaining work

`system.combat` uses independent synthetic packs through real loaders, command admission, Flecs, projections and save/restore. It checks firing/cancellation, turret target sharing, magazine/cost rules, damage conservation, repair work, destruction and continuation. Platform results and graphical smoke evidence are recorded in [combat verification](smoke/combat-stage-one.md).

Component faults, material deliveries, finite repair supplies, fires, atmosphere, casualties, missiles and shields remain later work. Balance, actual screen-reader operation, physical devices and six-display performance require separate evidence.
