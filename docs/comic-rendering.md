# Comic rendering integration

Implementation began after foundation commit `5a0dffe`, whose native/WASM/system/network CI and Pages deployment passed. The ignored browser prototype remains a visual reference; runtime implementation belongs to the shared C++ presentation layer.

## Material contract

Definitions schema 3 adds optional `render.sprite` and `render.engines`. Schema 1/2 packs retain their existing `images` billboard path. The existing positive `render.scale` is rendered height in world metres.

A sprite material contains `albedo`, optional `normal` and `depth` PNG references, optional `emissive` (default false), and `frames` (one or eight, matching `render.mode`). All maps share atlas dimensions. Each frame contains a unique `name`, integer `rect: [x,y,width,height]`, `pivot: [x,y]`, `azimuth` in degrees from bow around port, `depth_scale` (default zero), `depth_offset` (default zero), and optional `attachments`.

Depth is linear red, white nearer. A sample's camera-facing displacement is `render.scale * (depth_offset + (depth - 0.5) * depth_scale)`. Calibration is per view, scales with the entity and does not change simulation collision geometry. Normals are linear RGB view-space data (+X right, +Y up, +Z toward the viewer). Alpha coverage comes from albedo.

Attachments are named objects with frame-local `x`, `y`, signed integer `layer`, and optional `depth_offset` in rendered-height units. Layers are ordering hints, never physical depth. Engines contain a matching named `sprite` material plus positive `scale`, `power` and `range` parameters. No source sidecar is executed; `import_sprite_sidecar` converts its supported coordinate and image contract to pack data.

Maps may specify `lighting`: `sun_direction`, `sun_color` (three linear components), `sun_intensity`, `ambient`, `hatch_spacing`, `hatch_thickness`, `ink_strength`, `shadows`, `shadow_bias` and `shadow_softness`. Verification checks finite bounded values and nonzero sun direction. Defaults are selected by the renderer.

The permitted own-ship Console bundle copies only the verified material vocabulary and referenced images. It excludes world maps, scenario metadata, and System/controller internals. Existing save and archive identity behavior is preserved.

## Delivery record

- Material loading, validation, source-sidecar conversion and permitted Console bundle copying: implemented, with synthetic native and WASM content checks.
- Depth-aware renderer, shadows, propulsion presentation, Workshop controls and training content: in progress. Completion and graphical evidence will be recorded here as each step is verified.

## Accepted behavior

Entity transforms and calibrated pixel depth must determine lighting and GPU occlusion consistently. Engine light cones follow actual forward thrust and have no added radial halo. Coasting, disabled drives, braking and lateral thrust do not invent rear exhaust. Explosion rendering has an internal event interface but no trigger in gameplay or tools yet. Original `raw/` assets and the ignored prototype remain unchanged.
