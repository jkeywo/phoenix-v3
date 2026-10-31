# Switchblade pirate raider materials

30 October 2026. The pirate raider now uses the approved Switchblade drawings: updated front/rear elevations, side elevation v3, and front/rear three-quarter v2. Its inherited Cruiser-test and delivery variants share the replacement. The original drawings and extracted per-view sprites, normal/depth maps, atlases, sidecars and registration receipts are saved under ignored `raw/switchblade/`. Runtime materials are in `content/base/assets/switchblade/`.

## Preparation and geometry

Run `python scripts/prepare-switchblade-assets.py --install` explicitly with Pillow, NumPy and SciPy. Source artwork is copied into raw before processing. Paper removal changes alpha only. Integer cropping/placement preserves retained source RGB without resampling; individual frame rectangles normalise apparent ship height. Five source bearings supply eight runtime entries using renderer-supported mirroring for the opposite side.

Depth is a registered geometric approximation: longitudinal hull planes, bevelled cabin/prow surfaces and separate rounded engine cylinders. Camera-facing distance follows `F*cos(azimuth) + P*sin(azimuth)` with bow-positive F. It is independent of paint, ink, illumination and shadows. Bright exhaust cores are recessed behind nozzle rims. Normal maps come from continuous geometric Z derivatives before depth quantisation, using linear view-space +X right, +Y up, +Z toward the viewer. Five effective normal-channel bits bound vector error and limit transfer size.

The upper and lower gun drawings are extracted into separate eight-bearing materials. Their pivots share registered source coordinates with the hull sockets. The runtime's quantised hull-depth sample is subtracted from the desired physical mount Z to obtain each attachment offset. Each turret's depth offset cancels its own pivot's quantisation error. Signed integer layers only supply draw-order bias; depth controls hull/turret occlusion. Muzzle points are registered to the extracted barrels. Auxiliary beam/PDC mechanisms retain their existing operation with small visuals at separate sockets, rather than duplicating independently aimed guns at the main mounts. Two exhaust sockets reuse the existing effect; there remains one propulsion System.

Non-presentation raider data is checked unchanged by installation. Old pirate atlases are backed up under `raw/switchblade/retired-pirate-art/` and retired from the pack; the legacy turret visual identity now uses Switchblade art. Gameplay collision, ammunition, weapon behavior and equipment tuning are unchanged. Render height is 18 metres, independent of collision geometry.

These maps approximate visible volumes; they are not a recovered 3D mesh. They do not reconstruct hidden surfaces underneath extracted turrets. The hand-drawn bearings also contain small proportion differences, and mirrored views approximate opposite-side details.

## Verification and rendering evidence

- Numeric inspection passed for all nine atlas dimensions, 24 directional material entries, rectangle/pivot bounds and normal lengths (maximum length error below 0.06). Reconstructed turret-pivot Z error is below 2e-9 in local rendered-height units.
- The production packager and verifier accepted an isolated copy of the previously verified base with only the Switchblade presentation migration, together with Test Missions: 34 entity definitions, eight maps, 58 consoles. Its base archive is 62,917,377 bytes, below the existing 64 MiB limit.
- The manually invoked native OpenGL gallery loads the production Asteroid Duel through World, finds its raider scene projection and draws all eight bearings. Seven captured passes cover ordinary lighting, normal/depth diagnostics, turret traversal at 90/180/270 degrees and both exhaust effects. All four primary turret/exhaust sockets resolve through `SceneRenderer::attachment`. Captures and logs are under ignored `build/switchblade/`; the ordinary, traverse-90 and exhaust galleries were visually inspected.

Reproduce the explicit renderer smoke with:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_switchblade_gallery
./build/native/Release/phoenix_switchblade_gallery.exe build/switchblade/base.zip build/switchblade/test-missions.zip
```

The shared working-tree base currently references 40 unavailable rescue portrait assets; full current-source pack generation fails on the Courier's portrait reference. The isolated verification described above leaves those unrelated source changes untouched. This is not a successful rebuild of the complete current working tree. Browser rendering and a live network session were not exercised.
