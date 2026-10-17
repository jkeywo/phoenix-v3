# Foundation content format

This is schema 1 for the foundation vocabulary, not the complete GDD equipment schema. Packs contain `manifest.json`, `definitions.json` and PNG files under `assets/`. Packs use ZIP; their identity is SHA-256 of the exact archive bytes. Limits are 32 MiB archived, 16 MiB per entry, 64 MiB expanded and 1024 entries. File paths must be relative, shorter than 512 bytes, and have no trailing slash, traversal, backslashes or drive prefixes. Archives are decoded into bounded memory, never extracted to paths supplied by the archive.

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
