# Foundation content format

This is schema 1 for the foundation vocabulary, not the complete GDD equipment schema. Packs contain `manifest.json`, `definitions.json` and PNG files under `assets/`. Packs use ZIP; their identity is SHA-256 of the exact archive bytes. Limits are 32 MiB archived, 16 MiB per entry, 64 MiB expanded and 1024 entries. File paths must be relative, shorter than 512 bytes, and have no trailing slash, traversal, backslashes or drive prefixes. Archives are decoded into bounded memory, never extracted to paths supplied by the archive.

Definitions schema 2 also supports explicit Stations. Each entity's `stations` array contains `{id, consoles:[...]}`; each Console contains `{id, definition, systems:[...], feeds:[...]}`. `definition` resolves a Console layout. `systems` assigns exclusive control of fitted `engine`, `steering` and/or `radar`; `feeds` adds read access. Control also grants its own feed. Every fitted System has exactly one controlling Console; read-only Consoles may have empty `systems`. Station and Console identities are stable strings, unique within their ship, with at most 32 of each. These bindings describe support for the existing Systems, not additional gameplay equipment.

An explicit Station entity cannot also use legacy `console` or `controller`. Optional `controllers` maps fitted System names to existing tree definitions; each tree may output only to that System. Unclaimed Systems run independently. Existing entities normalize to one `main` Console with their original layout, all fitted System controls/feeds and original whole-ship controller behavior. No existing pack needs conversion. New ship rosters and playable layouts are not included in this change.

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

Host transfer, client cache and local sessions retain loaded pack handles instead of decoding or copying their archives again. Cache records are still hashed and loaded before use; only downloaded or repaired records are written. Session and fleet composition share in-memory container operations, with ZIP encoding at save/bundle boundaries. Power grids, services, jobs and broader GDD Systems are not yet supported and must not be represented as validated by this schema.

Definitions schema 3 adds calibrated sprite materials, signed attachments and scene lighting. See the [comic rendering contract and delivery record](comic-rendering.md). Legacy image billboards remain supported.

## Radar symbols

Optional `render.sprite.radar_icon` selects `ship`, `asteroid`, `sun` or `contact`. Legacy image materials use `render.radar_icon` instead. Missing data defaults to the generic contact ring; unknown names fail verification. Source sprite sidecars accept the same top-level `radar_icon` string, which imports into the canonical sprite material. Packaged editable examples sit beside the training PNGs as `assets/comic/*.sprite.json`; the original raw inputs remain unchanged.

These are abstract vector symbols, not reduced copies of detailed sprite art. Both native/browser radars and Workshop/GM maps use the same bounded geometry. Ship noses follow the observed heading (zero is right/east, positive turns towards screen up); rocks and suns remain upright. Glyphs retain a minimum screen size independent of physical entity scale or map zoom. Workshop Properties provides the **Radar icon** selector and a sample.

Only the small symbol selection accompanies each existing permitted body observation. Remote clients do not need the contact's sprite, definition or maps added to their own-ship presentation pack. No additional contacts, ranges, headings or classification inference are introduced; the current radar still reports authored definitions as before. Packets without the optional field display a generic contact. Save-state physics and simulation fingerprints are unchanged.

## Pre-game ship-slot requirements

A map ship slot may include `"mandatory": true` (boolean; omitted means false). Workshop exposes this as Mandatory ship slot. At new-game lobby launch, an unclaimed mandatory slot uses the first entry of `allowed_ships` under deterministic AI on every host; an unclaimed optional slot is absent. Claimed slots use the participant's allowed selection. Mandatory AI ships are part of the saved simulation but do not create fleet servers or quorum votes. Existing private fleet setup/recovery schema 1 remains readable; selected ships may now include AI-only slots outside the distinct server-to-slot ownership map. Recovery retains its exact saved ships rather than regenerating omitted optional slots.
