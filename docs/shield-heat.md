# Shield impact heat

Shield absorption produces transient hotspots on a spherical bubble around the ship. Only heated hex outlines are visible. An isolated reference hit starts white/yellow, cools through orange and red, and disappears within approximately two simulation seconds. Damage increases both heat and patch area. Nearby hits add into the same scalar heat field before colouring, so overlaps reheat and blend rather than stacking separately coloured flashes.

## Simulation and presentation

The production shield resolver totals actual absorbed damage across contributions and emits one `SceneShieldImpact` for that resolved impact. Bypassed damage, empty shields and rule-negated damage do not create heat. Direct shield-targeted effect damage also emits its actual absorbed amount. Audio configuration does not control visual emission.

Events carry a stable identity, tick/time, absorbed amount, ship-local outward direction and, for projectiles/beams, a point on the incoming path. The renderer intersects that line with the enclosing sphere, preserving off-centre strikes and elevated projectiles. Directional effects without a projectile use their existing incoming heading. The shell encloses the collision footprint and conservative sprite depth/pivot bounds or transformed model mesh bounds, with a ten-percent margin. Positions and directions stay attached to the ship as it moves and turns.

`World::scene()` supplies recent events in `SceneBody`; radar observations retain their existing contract. Callers continue to supply authorised scene bodies. The effect follows the renderer's hidden-body setting. Events are presentation output rather than saved gameplay state: restore starts with no historical shield heat, and normal scene clearing discards all GPU history. Simulation time controls cooling, so pause freezes heat and repeated draws cannot accelerate cooling or stamp an event twice.

History retains at most 128 impacts per ship for three seconds and is pruned by ordinary simulation ticks, independently of rendering. Each renderer has a maximum of 128 active shield texture pairs; new allocations beyond that cosmetic budget are omitted until space is available. No gameplay outcome depends on either limit.

## Renderer and tuning

The shared scene renderer owns two 256×128 RGBA8 render textures for each active bubble. A ping-pong pass subtracts elapsed cooling and stamps soft angular circles. Newly observed retained events replay in time order, producing the same overlapping heat after skipped snapshots. Angular distance crosses longitude seams naturally. RGBA8 cooling carries its fractional quantisation remainder so refresh rate does not determine fade duration. Expired or removed bodies release their textures.

The shell shader samples heat and draws an antialiased procedural hex lattice with no face fill. It draws after the scene with ordinary alpha blending, opaque depth testing and no depth writes. Both sprite and model backends use this pass, including clipped/offscreen views and camera-relative coordinates. Queued surrounding UI is flushed before resource preparation and texture updates. Model bodies and their attachment transforms now honour their projected altitude, keeping the hull and bubble together. Subpixel hexes fade to limit aliasing. The latitude/longitude lattice compresses near the poles; it is a cosmetic sphere surface rather than an equal-area geodesic mesh.

Artistic constants are grouped at the top of `src/graphics/scene/shield_scene.cpp`: reference damage 30, cooling time two seconds, reference angular radius 0.48 radians, radius limits 0.16–0.85 radians, 48 longitude hex columns and line width 0.035 lattice units. Radius scales with the square root of absorbed damage; heat and radius are bounded. These are editable presentation defaults, not balance assertions. Workshop authoring controls are deferred.

## Validation

The combat system fixture exercises layered/partial absorption, full bypass, depleted shields, repeated impacts, bounded history and save/restore independence through the production loader, resolver and scene projection. It uses synthetic equipment rather than shipped tuning.

The explicitly invoked `phoenix_comic_probe` includes synthetic sprite and model shield captures for hot/cooling/overlapping patches, attachment under motion/rotation, elevated seam impacts, repeated paused views, expiration and offscreen clipping. It remains outside automatic tests and production image goldens.

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_combat_tests,phoenix_comic_probe
./build/native/Release/phoenix_combat_tests.exe
Push-Location build/native/Release
./phoenix_comic_probe.exe
Pop-Location
./scripts/build.ps1 -Target web -BuildTarget phoenix_combat_tests,phoenix_comic_probe
# Run the WASM combat executable with the bundled Node runtime from build/web.
# Serve build/web and open phoenix_comic_probe.html in a WebGL 2 browser.
```

## Execution evidence — 29 October 2026

- Focused native and threaded WASM builds passed for `phoenix_combat_tests` and `phoenix_comic_probe`. Both combat suites passed, including the translated firing/target positions, and their two continuation hashes match after normalising Windows/browser line endings. Logs/traces are ignored under `build/shield-native-combat.*` and `build/shield-web-combat.*`.
- The complete native graphics probe passed on OpenGL 3.3, NVIDIA GeForce RTX 5090 Laptop GPU, driver 617.14. Both shield backends passed cooling, overlap, two-camera/pause deduplication, movement/rotation, seam/elevation, raised foreground occlusion, preview clipping, expiration and skipped-snapshot reconstruction. Existing comic/material/radar checks also passed. Log: `build/shield-native-gpu.log`; captures: `build/native/Release/shield-{sprite,model}-*.png`. Green capture borders deliberately mark the preview's clipping boundary.
- The final threaded browser probe passed in the Codex in-app Chromium browser using actual WebGL 2 / GLSL ES 3.00. Both shield backends and existing graphics checks passed. It was served locally with COOP/COEP headers; the diagnostic receipt is `build/shield-web-gpu.log`. Browser image exports remain in its transient WASM filesystem.
- Validation also caught and corrected the preview batch flush, the transient beam's missing path origin, a Windows probe library-order conflict and an existing typed-JSON comparison rejected by the WASM compiler. Synthetic fixture off-state recharge was made explicit rather than changing production tuning.

These are focused integration and explicitly invoked graphical results, not a full CI run or six-display performance/physical-device acceptance.
