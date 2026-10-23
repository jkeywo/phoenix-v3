# Gameplay sprite relief — 24 October 2026

The ten previously flat solid-object sprites now have registered linear depth and view-space normal maps: combat Courier hull/turret, pirate hull/turret, missile, shuttle, interceptor, bomber, rescue station and planet. The three directly authored Courier hull materials share the same images and calibration; inherited ships receive their resolved parent materials. Adjacent `.material.json` and importable `.sprite.json` files retain the same calibration, source rectangles, pivots, projected forward vectors and mirroring.

## Generation and calibration

The initial built-in imagegen drafts were rejected: their apparent lighting did not consistently encode camera distance, especially in diagonal views. None of those drafts is shipped or used by the bake. The replacement is a deterministic geometric proxy bake in `scripts/prepare-gameplay-relief.py`, using NumPy, SciPy and Pillow. It reads alpha coverage and authored yaw, never colour brightness, paint, ink or baked illumination. Colour PNGs remain unchanged.

Directional objects use a yawed ellipsoid registered to each source silhouette. The conditional camera-facing intersection includes the longitudinal depth gradient: the bow/left end is nearer in front-quarter views; the stern/right end is nearer in rear-quarter views. Broadside cross sections are convex. Mirrored bearings reuse their source maps, with reflection and normal-X reversal performed once by the renderer.

Depth is linear red, white nearer. The signed half-depth is `hypot(longitudinal_half_extent * cos(yaw), beam_half_extent * sin(yaw))`; `depth_scale` is twice that value divided by 0.92, reserving encoding headroom. `depth_offset` is zero, keeping the proxy centred on the entity origin across view changes. The half extents are authored in rendered-height units in the script. Each pixel encodes `0.5 + camera_facing_depth / depth_scale`; the renderer reconstructs world displacement with the existing `render.scale` contract.

The side-on station uses rounded local volumes inferred from silhouette thickness. The planet uses the front hemisphere of a sphere fitted to opaque coverage, with no continent/cloud relief. Torpedo flame pixels continue the adjacent nozzle depth; the existing baked exhaust colour remains intact. Visible attachments sit slightly above the sampled surface. Explicitly registered hidden Courier/pirate turret mounts use the corresponding proxy back surface, independently of the integer layer hints.

Normals are derived from the same continuous calibrated depth before its 8-bit quantisation, using a smoothed derivative in rendered-height units: `normalize(-dz/dx, +dz/dy_image, 1)`, encoded into linear RGB. This avoids contour rings from differentiating quantised Z. Five effective bits per RGB channel bound additional vector error to approximately three degrees while keeping the pack within the existing 32 MiB transfer limit. They use +X right, +Y up and +Z toward the viewer, matching the renderer. Background pixels are neutral; coverage continues to come exclusively from albedo alpha.

These are smooth geometric approximations. They supply coherent overall volume, lighting, occlusion and shadow depth, but do not recover the artwork's exact panel geometry, engine cavities or separate barrel meshes. A future geometry bake can replace them without changing the material contract or gameplay collision shapes.

## Reproduction

```powershell
python scripts/prepare-gameplay-relief.py
# Format the affected JSON with the repository's pinned Prettier.
./scripts/build.ps1 -Target native -BuildTarget phoenix_pack,phoenix_verify
./build/native/Release/phoenix_pack.exe content/base build/sprite-relief/base.zip
./build/native/Release/phoenix_pack.exe content/test-missions build/sprite-relief/test-missions.zip build/sprite-relief/base.zip
./build/native/Release/phoenix_verify.exe build/sprite-relief/base.zip build/sprite-relief/test-missions.zip
```

These explicit asset/verification commands are not new automatic gameplay tests or frozen production-content goldens.

## Verification

The rebuilt native production packager and verifier accepted the base plus Test Missions: 35 entities, eight maps and 20 consoles. The base ZIP remains below the existing 32 MiB archive limit; no loader or transfer limit was increased. Explicit numeric inspection checked all 20 image dimensions, normal-vector lengths, nonzero calibration, source-sidecar registration/forward vectors and front-/rear-quarter depth reversal for every directional sprite. The ten original colour PNGs remain byte-identical.

A 600-frame native OpenGL smoke reached the Rescue Viewscreen and its capture was inspected, including the lit spherical planet and station. This used the existing native executable, with the new material/image data over the previously bundled scenario definitions, isolating concurrent scenario changes. It is a rendering smoke, not an all-ten-sprite flight review. Logs, source/normal/depth review sheets and verification archives are under ignored `build/sprite-relief/`. Browser rendering and detailed per-panel geometry are not established by these checks.
