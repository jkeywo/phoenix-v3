# Foundation content format

This page describes the foundation fields and legacy definition boundary. Current fitted equipment uses [gameplay content](gameplay-content.md); decision ownership, command channels, complexity profiles and save compatibility use [Console Controllers](controller-content.md). All live gameplay uses the Controller runtime, including unambiguous converted foundation definitions.

This is schema 1 for the foundation vocabulary, not the complete GDD equipment schema. Packs contain `manifest.json`, `definitions.json` and PNG files under `assets/`. Packs use ZIP; their identity is SHA-256 of the exact archive bytes. Limits are 32 MiB archived, 16 MiB per entry, 64 MiB expanded and 1024 entries. File paths must be relative, shorter than 512 bytes, and have no trailing slash, traversal, backslashes or drive prefixes. Archives are decoded into bounded memory, never extracted to paths supplied by the archive.

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
tree programs or other machinery definitions. World/session saves use schema 7 with
Controller continuation; old authority formats are refused. Packet schema 5 refuses
older peers before interpreting commands or projections.

## Radar symbols

Optional `render.sprite.radar_sprite` is a PNG asset reference; legacy image billboards use `render.radar_sprite`. Workshop offers an asset picker, so mods can supply arbitrary icons. PNG verification uses the normal decoding/size bounds. Icons rotate with observed heading (zero right/east, positive towards screen up), preserve aspect ratio and keep a fixed readable screen size. Missing icons use a generic contact ring. The earlier `radar_icon` vector names remain readable for existing packs.

Source sidecars declare `files.radar_icon` alongside `files.sprite`, optional `normal` and `depth`. Import maps each filename to a pack asset and stores `radar_sprite` in the canonical material. `billboard_mode` is `camera` or `eight`, must match one/eight frames, and defaults to the frame count for older sidecars. `render_scale` sets world height and defaults to 1. Optional `engine_effect: {scale, range, power}` supplies plume settings when importing as an engine effect. Frame rectangles, pivots, bearings, depth calibration, attachments and emissive state retain their existing contract. Applying an invalid sidecar preserves the current render configuration.

Public radar PNGs are included in restricted crew presentation bundles under content-addressed `console-assets:assets/radar/<sha256>.png` references. The same address accompanies existing permitted body observations. Other ships' definitions, maps, controllers and detailed hull sprites remain excluded. This adds presentation data without expanding which contacts are observed.

The default **Phoenix Base Game** pack has ID `phoenix-base` and editable sources under `content/base/`. It supplies Courier, reusable scenery, the bridge Console, sprites and Free Flight. Shared templates retain existing `training.*` IDs for authored-map compatibility. **Phoenix Training** (`phoenix-training`, sources in `content/training/`) depends on `phoenix-base` and adds training scenarios, practice variants and patrol AI. Builds emit separate `base.zip` and `training.zip`, verifying the latter against the former. Both appear in Mod Packs; fresh libraries enable only the base. All packs can be disabled, including the base. Dependency toggles maintain the prerequisite selection and order.

Older installed combined base packs are split through the production loader: training definitions move into the training mod, preserving edits to them and retaining other authored maps in the base. Browser previews reflect that split before the C++ migration at the next hosting or Workshop entry. Existing activation choices are preserved; a migrated training pack inherits the old base's activation unless the user has already selected a different state. Synthetic system fixtures do not depend on either shipped pack.

## Pre-game ship-slot requirements

A map ship slot may include `"mandatory": true` (boolean; omitted means false). Workshop exposes this as Mandatory ship slot. At new-game lobby launch, an unclaimed mandatory slot uses the first entry of `allowed_ships` under deterministic AI on every host; an unclaimed optional slot is absent. Claimed slots use the participant's allowed selection. Mandatory AI ships are part of the saved simulation but do not create fleet servers or quorum votes. Existing private fleet setup/recovery schema 1 remains readable; selected ships may now include AI-only slots outside the distinct server-to-slot ownership map. Recovery retains its exact saved ships rather than regenerating omitted optional slots.
