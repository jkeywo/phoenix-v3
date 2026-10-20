# Comic rendering integration

Implementation began after foundation commit `5a0dffe`, whose native/WASM/system/network CI and Pages deployment passed. The ignored browser prototype remains a visual reference; runtime implementation belongs to the shared C++ presentation layer.

## Material contract

Current Workshop authoring uses arbitrary PNG radar sprites and complete sidecars; the [radar sprite contract](content-format.md#radar-symbols) supersedes the original vector-only selection.

The earlier radar-symbol increment added optional `radar_icon` to sprite materials and source sidecars. It selects shared vector ship, asteroid, sun or generic-contact geometry for native/browser radars and Workshop/GM maps. See the [radar symbol contract](content-format.md#radar-symbols). The manually invoked comic probe also writes `radar-icons-smoke.png`, showing the symbols at 12, 16, 20 and 24 pixels.

Radar-symbol verification on 18 October passed all 11 native integration cases, the WASM checks and native/WASM continuation comparison. The OpenGL/WebGL size chart was inspected, along with the actual browser Workshop map, gameplay radar and host-approved remote GM map. The first live remote crew join passed its symbol assertions; the later confirmed replacement reconnect again timed out as recorded below. Existing dense-contact overlap/label placement is unchanged. Captures are under `build/radar-*-browser.png` and `build/native/Release/radar-icons-smoke.png`. A running native window was left untouched; the updated game linked as `build/native/Release/native-radar.exe` for verification.

Definitions schema 3 adds optional `render.sprite` and `render.engines`. Schema 1/2 packs retain their existing `images` billboard path. The existing positive `render.scale` is rendered height in world metres.

A sprite material contains `albedo`, optional `normal` and `depth` PNG references, optional `emissive` (default false), and `frames` (one or eight, matching `render.mode`). All maps share atlas dimensions. Each frame contains a unique `name`, integer `rect: [x,y,width,height]`, `pivot: [x,y]`, `azimuth` in degrees from bow around port, `depth_scale` (default zero), `depth_offset` (default zero), and optional `attachments`.

Frames also support optional `mirror_x: true`; see the [mirroring contract and eight-bearing example](content-format.md#sprite-region-mirroring). Both ordinary and instanced colour/shadow passes reflect the source maps and pivot; normals reverse their horizontal component. CPU engine placement reflects the original marker around the source pivot and samples depth at the original marker coordinate. Existing conservative bounds remain valid under reflection.

Depth is linear red, white nearer. A sample's camera-facing displacement is `render.scale * (depth_offset + (depth - 0.5) * depth_scale)`. Calibration is per view, scales with the entity and does not change simulation collision geometry. Normals are linear RGB view-space data (+X right, +Y up, +Z toward the viewer). Alpha coverage comes from albedo.

Attachments are named objects with frame-local `x`, `y`, signed integer `layer`, and optional `depth_offset` in rendered-height units. Layers are ordering hints, never physical depth. Engines contain a matching named `sprite` material plus positive `scale`, `power` and `range` parameters. No source sidecar is executed; `import_sprite_sidecar` converts its supported coordinate and image contract to pack data.

Maps may specify `lighting`: `sun_direction`, `sun_color` (three linear components), `sun_intensity`, `ambient`, `hatch_spacing`, `hatch_thickness`, `ink_strength`, `shadows`, `shadow_bias` and `shadow_softness`. Verification checks finite bounded values and nonzero sun direction. Defaults are selected by the renderer.

The permitted own-ship Console bundle copies only the verified material vocabulary and referenced images. It excludes world maps, scenario metadata, and System/controller internals. Existing save and archive identity behavior is preserved.

## Delivery record

- Material loading, validation, source-sidecar conversion and permitted Console bundle copying: implemented, with synthetic native and WASM content checks.
- Shared renderer, sprite-depth occlusion, depth-displaced sun-shadow pass, actual-forward-thrust projection, cone lights and internal explosion rendering: implemented. The explicit synthetic GPU probe passes on native OpenGL 3.3 and browser WebGL 2, covering recessed/near pixel overlap, zero-depth fallback, cast shadows and directional/combined lighting.
- Workshop material/sidecar import, attachment and calibration controls, shared live preview, scene lighting and diagnostics: implemented. Browser import/export/reload of the comic training pack was exercised through its real file picker and download flow. Native Workshop capture was also inspected.
- Training Courier, asteroid, sun and thrust art is packaged in `content/base/assets/comic`. `training.lighting` demonstrates forward-exhaust illumination. The source PNGs remain in ignored `raw/`; ordinary builds use only tracked packaged assets. `scripts/prepare-comic-assets.py` explicitly regenerates calibrated depth and corresponding normals using Pillow. The legacy tug keeps its original single-image material and gameplay tuning is unchanged.

The colour pass draws directly into the existing clipped Viewscreen region. It reconstructs each fragment along the original camera ray and writes its projected depth, preserving the drawn silhouette. A shared 128×128 displacement grid supplies rasterisation and sun-shadow geometry; alpha cuts out uncovered pixels. The 2048×2048 shadow map uses nine-tap filtering. This bounds geometry work but approximates sub-grid relief in the shadow silhouette. The nearest sixteen local lights are selected stably around the camera target. Renderer coordinates are rebased around that target to limit world-coordinate precision loss.

Hatching uses a quantised, object-wide level of detail based on projected size. This keeps ink stationary under changing light and avoids turning source-resolution strokes into subpixel grey noise at a distance.

`SessionPort::scene()` supplies `SceneBody` values with an additional 0–1000 actual forward propulsion output. `BodyView` and radar observations retain their previous vocabulary; remote Console clients do not gain a full-world scene. Applied acceleration is measured in the production engine execution path, with zero rear output during braking, coasting or disablement. Synthetic native/WASM simulation checks cover these transitions.

`SceneRenderer::explosion(ExplosionEffect)` accepts bounded presentation-only events. No gameplay, GM or player action calls it. The manual synthetic probe exercises its envelope, colour and coexistence with engine lighting.

The authoring probe also checks a smaller offscreen target. Target creation and scissor bounds preserve that framebuffer independently of the native/window dimensions. The browser picker retains its hidden file input until completion/cancellation so the same owned import path can be used reliably on both platforms.

```powershell
# Explicit graphical evidence; deliberately absent from the automatic test list.
./scripts/build.ps1 -Target native -BuildTarget phoenix_comic_probe
Push-Location build/native/Release
./phoenix_comic_probe.exe
Pop-Location
./scripts/build.ps1 -Target web -BuildTarget phoenix_comic_probe
# Serve build/web and open phoenix_comic_probe.html in a WebGL 2 browser.
```

## Accepted behavior

Entity transforms and calibrated pixel depth must determine lighting and GPU occlusion consistently. Engine light cones follow actual forward thrust and have no added radial halo. Coasting, disabled drives, braking and lateral thrust do not invent rear exhaust. Explosion rendering has an internal event interface but no trigger in gameplay or tools yet. Original `raw/` assets and the ignored prototype remain unchanged.

## Evidence — 18 October 2026

- Full local automatic gate passed: 11 native integration cases, WASM checks, native/WASM continuation comparison and all six real network journeys. This includes content round trips, privacy, deterministic simulation and recovery. Graphical checks remain explicitly invoked smoke evidence.
- Final reruns again passed the 11 native cases and all WASM/continuation checks, but two full network runs timed out during the confirmed replacement reconnect (`service/test.mjs`, live-session journey). That journey passed when run alone. The suite is therefore not consistently green; the cause is unconfirmed, with a stale direct route a working hypothesis. No networking assertion was relaxed and no transport behavior was changed as part of this rendering increment. Logs: `build/comic-final-gate.log`, `build/comic-network-final.log`, `build/comic-network-session-recheck.log`.
- The synthetic GPU probe passed on OpenGL 3.3 and actual browser WebGL 2: recessed per-pixel occlusion, flat fallback, depth-displaced sun shadows, nested preview target, aft-only cone lighting and an internal explosion event combined with engines. Initial browser checks caught the WebGL 1 default and a cross-stage integer-precision mismatch; the browser baseline is now explicitly WebGL 2.
- In the browser Workshop, the training pack loaded, `training.lighting` rendered through the shared preview, zero/full thrust visibly removed/restored the asteroid's warm lighting, and export followed by reopening that downloaded ZIP succeeded. No asteroid-specific shader exception or radial halo is used. The preview's thrust value is deliberately isolated from gameplay.
- The actual browser Ship server loaded that exported pack, started `training.lighting`, claimed a Console and rendered its Viewscreen. Increasing requested speed produced the aft plume and warm asteroid illumination during actual acceleration. Native local play also loaded the same browser-exported ZIP and rendered the scene; this goes beyond the isolated authoring preview.
- A manually invoked synthetic six-region workload rendered 101 placed bodies plus a spawned ship with depth-backed materials and sun shadows, alongside five active Consoles. At 5760×2160 on the NVIDIA GeForce RTX 5090 Laptop GPU, driver 617.14, 1,800 samples recorded frame p95 **9.09 ms**, maximum **10.52 ms**. This is CPU wall/presentation evidence on the available three-monitor machine, not isolated GPU timing, a shipped-content benchmark or six physical displays.
- Captures/logs remain ignored under `build/`, including `comic-workshop-browser.png`, `comic-integration-gate.log`, native `comic-*-smoke.png` and `bridge-smoke/six-regions-comic.json`. Generate the independent heavier workload with `tests/manual/comic_bridge_fixture.py`, then use the normal six-region command with `bridge-comic.zip`.

Depth remains an authored visual approximation with one visible surface per view. Shadows approximate relief with a bounded grid, and hidden surfaces are absent. Per-view calibration and derived normals improve the existing draft maps; this is not a watertight geometry bake or a collision-model change. Touchscreen, physical controllers, readers and off-machine networking retain the foundation acceptance limits in `mvp-status.md`.

Per-frame horizontal mirroring was verified on native OpenGL and browser WebGL 2 on 20 October 2026; see the [separate integration and graphical evidence](smoke/sprite-mirroring.md).
