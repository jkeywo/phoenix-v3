# Foundation content format

Maps may set `presentation.skybox` to a pack-qualified PNG reference, for example
`"phoenix-base:assets/skyboxes/settled-dark-v1.png"`. It must decode as a 2:1
equirectangular panorama or a vertical six-face cube strip (+X, -X, +Y, -Y, +Z, -Z) within the ordinary image limits. Omitted or empty means
no skybox; there is no inherited base-game fallback. Invalid types, missing assets,
bad pixels and unsupported image layouts are verification errors naming the map field.
The reference participates in scenario asset preparation and ordinary dependency,
ZIP/DAT and scenario-export handling. It affects the world Viewscreen background,
not loading-screen artwork, lighting or the Workshop map grid.

Source ZIPs and compiled DAT packages share this logical content format. Workshop packages both directly; see [packaging and loading assets](asset-loading.md) for encoding, limits and preload declarations.

This page describes the foundation fields and legacy definition boundary. Current fitted equipment uses [gameplay content](gameplay-content.md); decision ownership, command channels, complexity profiles and save compatibility use [Console Controllers](controller-content.md). All live gameplay uses the Controller runtime, including unambiguous converted foundation definitions.

This is schema 1 for the foundation vocabulary, not the complete GDD equipment schema. Packs contain `manifest.json`, `definitions.json` and PNG files under `assets/`. Packs use ZIP; their identity is SHA-256 of the exact archive bytes. Limits are 64 MiB archived, 16 MiB per entry, 64 MiB expanded and 1024 entries. File paths must be relative, shorter than 512 bytes, and have no trailing slash, traversal, backslashes or drive prefixes. Archives are decoded into bounded memory, never extracted to paths supplied by the archive.

Definitions schema 2 also supports explicit Stations. Each entity's `stations` array contains `{id, consoles:[...]}`; each Console contains `{id, definition, systems:[...], feeds:[...]}`. `definition` resolves a Console layout. In legacy definitions without `control_layout`, `systems` assigns exclusive ownership of fitted `engine`, `steering` and/or `radar`; `feeds` adds read access. Ownership also grants its own feed and supplies the conversion boundary's command owner. Read-only Consoles may have empty `systems`. Station and Console identities are stable strings, unique within their entity, with at most 32 of each. Explicit layouts instead declare Controller command subscriptions separately from observation ownership.

An explicit Station entity cannot also use legacy `console` or `controller`. Legacy `controllers` may map fitted System names to Controller-context trees using the generated owned channels. At load, unambiguous ownership converts to Console-owned Controllers and explicit physical subscriptions. Timed object-state trees and whole-entity `controller` policies are retired; author explicit Controller layouts and hierarchical trees. A legacy single Console normalizes to `main`. System-context decision trees, summary-intent presets and ambiguous ownership require explicit migration and are refused with diagnostics. There is no separate legacy or NPC runtime.

Manifest example:

```json
{"schema":1,"id":"example-mod","dependencies":["base"],"overrides":["base.ship"]}
```

Pack IDs use lower-case letters, digits, dots, underscores and hyphens. Select packs in dependency order. Later definitions replace earlier definitions only when the manifest explicitly names each replaced ID in `overrides`. A declared override must actually replace a definition of the same kind. Definitions share an ID namespace. Inheritance resolves after all overrides, so a derived definition sees the final selected parent. `extends` uses JSON merge-patch: objects merge, arrays replace, and null removes a field. Missing parents and cycles are errors.

`definitions.json` contains `schema:1` and optional `entities`, `maps` and `consoles` arrays. The independent [synthetic fixture](../tests/fixtures/content/definitions.json) demonstrates the encoding; it is not shipped balance content. Asset references have the form `pack-id:assets/file.png`.

- Entity definitions declare render mode `camera` (one image) or `eight` (eight images), positive render scale, supported `systems`, and circle/box collision dimensions/mass. Mass zero denotes static scenery. Supported systems currently declare engine maximum speed/acceleration, steering rate and radar range. Ship-slot eligibility requires positive mass, engine and steering. A console reference is optional but must resolve when supplied.
- Map definitions contain placed entities with stable numeric IDs, definition references, integer millimetre coordinates and millidegree headings. Named ship slots specify the same transform and a nonempty list of allowed ship definitions.
- Console definitions contain named widgets of kind radar, throttle, heading or status, with supported typed bindings and required semantic labels. Layout coordinates use an authored 1000 by 1000 area. Bounds are verified; responsive layout/runtime display is a later integration.

Verification resolves the final catalogue, decodes referenced PNG images, checks interfaces/references, validates supported ranges and rejects invalid bindings. PNGs are limited to 8192 pixels per dimension and 16 megapixels. Integer position/rate limits protect the foundation numeric representation; they do not prescribe ship tuning. The same fixture is verified after changing speed without changing an expected performance value.

`phoenix_verify.exe base.zip [mod.zip ...]` verifies an ordered selection and reports pack hashes and definition counts. Loaded `Pack` objects are immutable shared handles to their manifest, files and exact original archive. `PackDraft` is the separate editable form; it has no archive identity. Workshop validates drafts directly and encodes only on export. Selecting exported bytes creates a new loaded identity. Catalogue verification decodes each referenced PNG once per asset path in that verification pass.

Host transfer, client cache and local sessions retain loaded pack handles instead of decoding or copying their archives again. Cache records are still hashed and loaded before use; only downloaded or repaired records are written. Session and fleet composition share in-memory container operations, with ZIP encoding at save/bundle boundaries. Power grids and other fitted mechanisms use the separate gameplay vocabulary; this foundation description does not establish support for all GDD Systems or jobs.

Definitions schema 3 adds calibrated sprite materials, signed attachments and scene lighting. See the [comic rendering contract and delivery record](comic-rendering.md). Legacy image billboards remain supported.

Optional map `presentation.dust` and entity `render.reference_grid` provide render-only navigation aids, including platform dust overrides, custom textures and Workshop tuning. See [navigation aids](navigation-aids.md) for fields, defaults, camera behaviour and verification evidence.

## Fitted System identities

System object keys are ship-local instance identities. Optional `kind` names the
supported capability (`engine`, `steering`, `radar`); omitted `kind` retains the
legacy key-as-capability behavior. For example, `port` and `starboard` can both
declare `kind: "engine"` with independent mechanism settings. At most 256
instances are accepted per entity. Console `systems` and `feeds` bind exact
identities. Explicit Controller command channels bind physical consumers separately
from their observation-owning Console; two engines may consume one shared throttle.

Widgets may select an exact `controller` and `channel`; older `system` widgets
must resolve a unique compatible owned channel. Legacy unqualified commands are accepted only
for converted definitions with exactly one owned matching channel. Feed access grants no command
authority. The capability masks retained on older interfaces are presentation
hints, not the instance command boundary.

Restricted crew packs retain only public instance kind/bounds and verified
Console/widget bindings and public Controller channel/profile metadata, without private
tree programs or other machinery definitions. World/session saves use schema 13 with
Controller continuation; old authority formats are refused. Packet schema 5 refuses
older peers before interpreting commands or projections.

## Radar symbols

Optional `render.sprite.radar_sprite` is a PNG asset reference; legacy image billboards use `render.radar_sprite`. Workshop offers an asset picker, so mods can supply arbitrary icons. PNG verification uses the normal decoding/size bounds. Icons rotate with observed heading (zero right/east, positive towards screen up), preserve aspect ratio and keep a fixed readable screen size. Missing icons use a generic contact ring. The earlier `radar_icon` vector names remain readable for existing packs.

Source sidecars declare `files.radar_icon` alongside `files.sprite`, optional `normal` and `depth`. Import maps each filename to a pack asset and stores `radar_sprite` in the canonical material. `billboard_mode` is `camera` or `eight`, must match one/eight frames, and defaults to the frame count for older sidecars. `render_scale` sets world height and defaults to 1. Optional `engine_effect: {scale, range, power}` supplies plume settings when importing as an engine effect. Frame rectangles, pivots, bearings, depth calibration, attachments and emissive state retain their existing contract. Applying an invalid sidecar preserves the current render configuration.

Public radar PNGs are included in restricted crew presentation bundles under content-addressed `console-assets:assets/radar/<sha256>.png` references. The same address accompanies existing permitted body observations. Other ships' definitions, maps, controllers and detailed hull sprites remain excluded. This adds presentation data without expanding which contacts are observed.

The default **Phoenix Base Game** pack has ID `phoenix-base` and editable sources under `content/base/`. It supplies Courier, reusable scenery, the bridge Console, sprites and Free Flight. Shared templates retain existing `test.*` IDs for authored-map compatibility. **test** (`test`, sources in `content/test/`) depends on `phoenix-base` and adds training scenarios, practice variants and patrol AI. Builds emit separate `base.zip` and `test.zip`, verifying the latter against the former. Both appear in Mod Packs; fresh libraries enable only the base. All packs can be disabled, including the base. Dependency toggles maintain the prerequisite selection and order.

Older installed combined base packs are split through the production loader: training definitions move into the training mod, preserving edits to them and retaining other authored maps in the base. Browser previews reflect that split before the C++ migration at the next hosting or Workshop entry. Existing activation choices are preserved; a migrated training pack inherits the old base's activation unless the user has already selected a different state. Synthetic system fixtures do not depend on either shipped pack.

## Pre-game ship-slot requirements

A map ship slot may include `"mandatory": true` (boolean; omitted means false). Workshop exposes this as Mandatory ship slot. The optional `default_ship` must name an entry of `allowed_ships`; omission uses the first allowed ship. At an ordinary ship-host lobby launch, an unclaimed mandatory slot uses its default under deterministic AI on every host; an unclaimed optional slot is absent. A GM-hosted game fills every unclaimed slot with its default AI ship. Claimed slots use the participant's allowed selection. AI ships are part of the saved simulation but do not create fleet servers or quorum votes. A GM coordinator has one replication vote and an empty slot in the private ownership map, with no player Console leases. Existing private fleet setup/recovery schema 1 remains readable; new GM setups declare `gm_host`. Recovery retains its exact saved ships rather than regenerating omitted optional slots.

## Region environments and sprite entries

See [regions, fields and radar](regions-and-fields.md) for region fog, field generation, public map markers, entity/radar tags, sprite-sheet entries and radar sweep settings. Existing definitions without these optional fields retain their legacy defaults.

## Sprite region mirroring

Sidecar frames and canonical material frames accept optional boolean `mirror_x`, defaulting to `false`. It reflects the selected region horizontally around its pivot, including albedo, depth, normal orientation and engine attachment placement. Keep rectangles, pivots and markers in the original source coordinates. Attachment names, signed layers and depth offsets stay as authored. Engine-effect frames choose their own mirror flags independently. One-frame materials and named sprite entries support the same flag; standalone single-image sidecars may declare it at the top level.

Eight-direction sprites still declare eight unique frame names and explicit bearings. Three can reuse existing rectangles with `mirror_x: true`, so only five drawings are needed. No missing directions are generated automatically. For a 500Ã—100 atlas containing five 100Ã—100 drawings, use these frame entries, each with `pivot: [50, 50]`:

| Frame name      | Sidecar `camera_azimuth_degrees` / canonical `azimuth` | `rect`               | `mirror_x` |
| --------------- | ------------------------------------------------------ | -------------------- | ---------- |
| bow             | 0                                                      | `[0, 0, 100, 100]`   | `false`    |
| port-bow        | 45                                                     | `[100, 0, 100, 100]` | `false`    |
| port            | 90                                                     | `[200, 0, 100, 100]` | `false`    |
| port-stern      | 135                                                    | `[300, 0, 100, 100]` | `false`    |
| stern           | 180                                                    | `[400, 0, 100, 100]` | `false`    |
| starboard-stern | 225                                                    | `[300, 0, 100, 100]` | `true`     |
| starboard       | 270                                                    | `[200, 0, 100, 100]` | `true`     |
| starboard-bow   | 315                                                    | `[100, 0, 100, 100]` | `true`     |

For example, the final entry in `sprite_sheet.frames` is:

```json
{
  "name": "starboard-bow",
  "rect": [100, 0, 100, 100],
  "pivot": [50, 50],
  "camera_azimuth_degrees": 315,
  "mirror_x": true
}
```

Canonical frames use `azimuth` and `attachments` instead of sidecar bearings and markers. Their rectangle, pivot and mirror flag have the same meaning. An asymmetric source pivot at X=30 in a 100-pixel region displays at X=70; a source attachment at X=80 displays at X=20. Its displacement from the pivot changes from +50 to âˆ’50. Existing unmirrored packs need no migration, and mirroring does not change physical collision shapes or gameplay heading.

Map packs may declare fixed factions and mutual relationships, with per-placement and ship-slot membership overrides. Shared action trees and their referenced execution subtrees participate in normal dependency closure. See [Controller actions and factions](controller-actions.md).

## Shield effect catalogue

Author reusable shields in the top-level `effects` array. Effect IDs share the normal definition namespace. Effects use the same explicit pack overrides and `extends` inheritance as equipment; overrides resolve before consumers compile. A shield System references the effect through `shield_effect`. Author power stores, enabled intent, faults and physical compartments on the fitted System.

```json
{
  "id": "example.shield",
  "kind": "shield",
  "duration_ticks": 0,
  "contributions": {
    "fore": {
      "capacity": 100,
      "initial": 50,
      "bearing_mdeg": 0,
      "arc_mdeg": 180000,
      "recharge_per_second": 5,
      "off_recharge_per_second": 1,
      "energy_per_strength": 1,
      "sustain_energy_per_second": 2,
      "warmup_ticks": 20,
      "discharge_per_second": 5,
      "discharge_efficiency_permille": 1000,
      "layer": 0
    }
  }
}
```

Each contribution name owns an independent reserve and warmup. A definition supports at most 64 named contributions. Omitted duration or zero denotes an indefinite grant; a positive duration expires in simulation ticks. Scenario actions `apply_shield_effect`, `refresh_shield_effect` and `remove_shield_effect` use an explicit grant name. Refresh preserves reserve and accounting state.

The optional contribution `modifiers` object accepts `recharge`, `sustain`, `discharge` and `protection` percentage deltas in permille. Values add to officer/fault contributions before applying the property once. Fixed capacity, arc, layer and conversion parameters cannot change through these live modifiers.

Pre-damage modification uses `pre_damage_permille` (0..1000, 1000 by default; zero negates presented damage). `post_damage` schedules secondary damage after resolution. Other undeclared fields and unsupported operations fail validation. Workshop exposes effect templates and equipment reference selection; private effect definitions stay outside presentation-only client packs.

Station Console instances opt into component observations with `shield_feed: true`. This exposes permitted shield contributions independently of equipment feeds and grants no commands. Maintained shield-operating Consoles declare this explicitly; presentation companions preserve the flag.

A contribution can declare `reactions`, an array of atomic groups. Each group contains `operations` and optionally `payer` (a fitted equipment ID) with `energy` (a nonnegative cost). Operation `kind` is `damage`, `apply` or `remove`; `target` is `attacker` or `protected`. Damage requires positive `damage`; apply requires catalogue `effect` and stable local `grant`; remove addresses `grant`. Applying a modifier effect also requires `equipment`, an explicit compatible fitted System ID on the chosen target ship. Shield-grant applications omit `equipment`. Unsupported operations fail validation. Reactions execute after the original damage, and failure cannot rewind it.

Grant changes must precede damage operations inside a group. They are prepared together with the payment, then damage resolves against the prepared grant state. Secondary damage executes in the next tick's reaction phase, before ordinary ordnance and destruction processing. Events created during that phase wait for a later phase; they cannot recursively drain the current queue. Bounded execution results retain success/failure diagnostics. See the [shield implementation record](shield-migration-evidence.md) for coverage and limits.

## Modifier effect catalogue

The same top-level `effects` array accepts `kind: "modifier"`. Its `contributions` object contains 1–64 stable names; each value declares a supported native `property`, signed integer `delta_permille` in -100000..100000, and optional `role` (defaults to the property ID). Unknown properties and fields fail verification. `duration_ticks` is an integer in 0..1000000; omitted/zero means indefinite. Definitions participate in normal inheritance, explicit overrides, dependency closure and Workshop export/reload.

```json
{
  "id": "example.field-expertise",
  "kind": "modifier",
  "contributions": {
    "active": {"property": "recharge", "role": "field", "delta_permille": 250},
    "inactive": {"property": "off_recharge", "role": "field", "delta_permille": 250}
  }
}
```

Reference this definition from a Personnel trait with `"effects": ["example.field-expertise"]`. A duty binds its role using `"bindings": {"fore-shield": ["field"]}`. This applies both named contributions to that fitted shield. Distinct equipment can share a role. Repeated role entries on the same target are invalid. An effect group must have all required roles bound or none; an inapplicable trait is valid. Team bindings require the corresponding repair, Medical or security capability. Production authoring no longer accepts trait `bonuses` or duty `effects` maps. Trait identity remains available for qualification independently of its effect list.

| Property                             | Compatible native owner and physical meaning                                  |
| ------------------------------------ | ----------------------------------------------------------------------------- |
| `propulsion`                         | Engine, lateral thrust, boost or impulse acceleration.                        |
| `speed`                              | Engine, boost or impulse speed.                                               |
| `turn`                               | Steering or an engine's integrated turn rate.                                 |
| `sensors`                            | Radar equipment range, including long-range and directed scanner acquisition. |
| `cycling`                            | Weapon cycle rate, not projectile damage.                                     |
| `traverse`                           | Turret angular traversal rate.                                                |
| `reload`                             | Magazine replenishment work, not logistics delivery or weapon loading.        |
| `processing`                         | Work rate of a generic processor owned by the targeted equipment.             |
| `recharge`, `off_recharge`           | Shield strength recharge in the corresponding operating state.                |
| `sustain`, `discharge`, `protection` | Shield sustain energy rate, discharge rate and available protection.          |
| `repair`, `medical`, `security`      | Matching team work or the eligible deployed participant's native work.        |
| `comms`                              | Radio range.                                                                  |

Property deltas add against the unmodified base, including officer, scenario and applicable native-source adapters. A nonnegative property with a total delta at or below -1000 becomes zero. Zero bases remain zero. The shared integer calculation checks its aggregate delta within ±1000000000000, above the bounded roster and application-store maximum, and saturates against the property's limit before multiplication can overflow. Stored quantities, shield capacity/geometry/layers, warmup and conversion ratios are not live modifier properties. Native descriptors record the evaluation units and upper limits: acceleration, speed, range, sustain and relative work/cycle rates cap at 1000000000; turn/traverse at 360000 millidegrees per second; recharge at 1000000000 milli-strength per second; discharge at 1000000 strength per second; protection at 1000 permille; and high-resolution repair at 1000000000000000000 native work units. The repair/recharge units preserve intermediate progress precision; these limits are representational bounds, not shipped tuning targets. Personnel's percentage factor is shown before this native physical clamp.

Scenario tree actions are `apply_modifier_effect(ship, equipment, definition, grant)`, `refresh_modifier_effect(ship, equipment, grant)` and `remove_modifier_effect(ship, equipment, grant)`. Each application binds the complete effect to one compatible target; duties provide the multi-role equipment mapping for officer-maintained applications. Reapplying the same source/grant, definition and target refreshes without stacking or shortening expiry. Independent sources add. Refresh requires an existing grant. Removal is selective and never reverses completed physical work. Deferred shield reactions use the same prepared application lifecycle and next-tick queue.

Modifiers keep stable source, definition, application, target and contribution identities in saved state; derived totals are reconstructed. Officer contributions are maintained from current assignment, travel, health and deployment facts, rather than restored as independent copies. Effect definitions remain private in presentation-only client packs. See [Duty officers](duty-officers.md) and the [property-effect migration plan](property-effects-migration-plan.md) for integration boundaries.

## Resources, stores and recipes

The top-level arrays `resources` and `recipes` share the normal definition namespace, pack dependency order, explicit overrides and `extends` inheritance. Resource definitions require `id`, `label`, nonempty `tags` and `sizes`, a map from capacity tag to positive integer size. Optional `effects` contains inert effect IDs: storing, moving or consuming a resource does not apply them. An equipment consumer decides when and where to use attached effects. An ammunition resource may also declare `ammunition` with optional `fire_ticks`, `load_ticks` and native `projectile`; omitted profile values retain the directed operating mode's authored fallback. See [directed modes and payload authoring](directed-systems-content.md) for weapon/scanner bindings, explicit hit outcomes and periodic conditions.

A common `stock` declares `capacity_tag`, positive `capacity`, a `filter`, optional `single_resource`, `selection` and `initial` resource quantities. The filter accepts exact `resources` IDs and/or `tags`; default matching is their union. `require_all: true` requires every tag and membership in the exact list when present. Every accepted resource needs a positive size for the store's capacity tag. All resources share capacity through the sum of quantity multiplied by that size. Single-resource stores retain a selected identity even while empty; an exact singleton filter fixes it. Changing selection requires an empty store with no outstanding claims.

Recipes declare `label`, optional `tags`, positive integer `work`, exact `inputs` and nonempty exact `outputs`. Quantities are positive integer units; tag substitutions are not recipe ingredients. An empty input map is an explicit generated-resource recipe. Entity `processors` bind `id`, fitted `equipment`, allowed `recipes` IDs and/or `recipe_tags`, an initial `recipe` or `initial_recipe`, `mode` (`batch` or `continuous`), `work_per_second` and optional `energy_per_second`. `inputs` and `outputs` map each resource ID to an explicit local store address. Addresses can use an unambiguous authored store ID or `logistics:ID`, `system:MAGAZINE`, `energy:ID`, `coolant:ID`. Work and operating energy are separate from ingredient stock.

Continuous processors support an exact integer ratio period of at most 4096: let T be recipe work multiplied by 1000, and divide T by the greatest common divisor of T and every input/output quantity. The resulting period must be at most 4096 for every supported recipe. For example, work 1 with input 2 and outputs 4 and 2 has period 500; work 20 with input 2 and output 1 has period 20000 and requires batch mode or different authored ratios. Compilation rejects unsupported continuous periods with the recipe identity and calculated period. Batch recipes have no ratio-period restriction. Accepted continuous recipes retain exact quantities and saved fractional progress; the runtime does not silently truncate a search or discard output.

Magazines declare a single-resource `stock` and optional `processor`. The compiler supplies the magazine's fitted equipment and default output binding to its own stock. Native reload timing remains a modifier of magazine processing work. Generic processor work accepts the `processing` property. Magazine-local `capacity`, `initial`, `reload`, `ammunition_type` and projectile profile fields are retired. Electrical stores explicitly bind `resource`; coolant sources explicitly bind both `resource` and `capacity`. Their resource sizes are one in the native `energy` or `coolant` dimension.

Workshop edits the catalogue, stock filters, selected identities, recipes and processor bindings through its existing draft flow. Export closure follows resource IDs in quantity maps, inert effect references, recipe selections and tag-selected definitions. The maintained identity decisions and batch-size audit are recorded in [resource identity migration](resource-identity-migration.md).
