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
- Shared renderer, sprite-depth occlusion, depth-displaced sun-shadow pass, actual-forward-thrust projection, cone lights and internal explosion rendering: implemented. The explicit synthetic GPU probe passes on native OpenGL 3.3 and browser WebGL 2, covering recessed/near pixel overlap, zero-depth fallback, cast shadows and directional/combined lighting.
- Workshop controls and training content: in progress. Completion and graphical evidence will be recorded here as each step is verified.

The colour pass draws directly into the existing clipped Viewscreen region. It reconstructs each fragment along the original camera ray and writes its projected depth, preserving the drawn silhouette. A shared 128×128 displacement grid supplies rasterisation and sun-shadow geometry; alpha cuts out uncovered pixels. The 2048×2048 shadow map uses nine-tap filtering. This bounds geometry work but approximates sub-grid relief in the shadow silhouette. The nearest sixteen local lights are selected stably around the camera target. Renderer coordinates are rebased around that target to limit world-coordinate precision loss.

`SessionPort::scene()` supplies `SceneBody` values with an additional 0–1000 actual forward propulsion output. `BodyView` and radar observations retain their previous vocabulary; remote Console clients do not gain a full-world scene. Applied acceleration is measured in the production engine execution path, with zero rear output during braking, coasting or disablement. Synthetic native/WASM simulation checks cover these transitions.

`SceneRenderer::explosion(ExplosionEffect)` accepts bounded presentation-only events. No gameplay, GM or player action calls it. The manual synthetic probe exercises its envelope, colour and coexistence with engine lighting.

```powershell
# Explicit graphical evidence; deliberately absent from the automatic test list.
./scripts/build.ps1 -Target native -BuildTarget phoenix_comic_probe
./build/native/Release/phoenix_comic_probe.exe
./scripts/build.ps1 -Target web -BuildTarget phoenix_comic_probe
# Serve build/web and open phoenix_comic_probe.html in a WebGL 2 browser.
```

## Accepted behavior

Entity transforms and calibrated pixel depth must determine lighting and GPU occlusion consistently. Engine light cones follow actual forward thrust and have no added radial halo. Coasting, disabled drives, braking and lateral thrust do not invent rear exhaust. Explosion rendering has an internal event interface but no trigger in gameplay or tools yet. Original `raw/` assets and the ignored prototype remain unchanged.
