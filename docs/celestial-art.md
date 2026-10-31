# Celestial art and asteroid fields

The twelve illustrated asteroids, sun and Cairn planet use the Phoenix comic direction: heavy inks, flat colour and distinct silhouettes. Packaged albedos, normal/depth maps, materials and editable sprite sidecars live in `content/base/assets/celestial/`.

## Distribution

The Rescue under threat map's west, east and north debris fields and the Test Missions asteroid-duel field use the same twelve weighted definitions:

| Group    | Source set in ignored `docs/art/`                                                     | Chance per design | Combined |
| -------- | ------------------------------------------------------------------------------------- | ----------------- | -------- |
| Common   | `phoenix-generic-asteroid-01-v1.png` through `04`                                     | 20%               | 80%      |
| Uncommon | `phoenix-uncommon-asteroid-01-layered-v1.png`, `02-saddle`, `03-offset`, `04-scarred` | 4%                | 16%      |
| Rare     | `phoenix-asteroid-01-crag-v1.png`, `02-splinter`, `03-peanut`, `04-arch`              | 1%                | 4%       |

Weights total 100 and use the existing seeded weighted selection path. They are probabilities per generated asteroid, not quotas guaranteeing these proportions in a finite field. Existing field seeds, placement, density, clustering, loading distances, spin and collision settings are unchanged. Legacy individual placements of `base.asteroid` use common design 01. Independent development/test-only field artwork remains independent of production art.

`base.planet-billboard` now uses `phoenix-planet-v1.png`; `base.delivery-planet` inherits it. `base.sun` and `base.system-star` use `phoenix-sun-v1.png`; the rescue mission's `base.delivery-star` inherits the new emissive billboard instead of the previous procedural star effect. Existing entity positions, scales and collision definitions remain unchanged.

## Preparation and geometry

Run `python scripts/prepare-celestial-assets.py --install` with Pillow, NumPy and SciPy installed. This explicit authoring step needs the original ignored `docs/art/` images. Builds use the packaged assets directly and never need image generation or those Python libraries.

Background removal uses a pale-paper colour mask flooded from image edges, plus registered seeds inside the rare arch and the sun's flare loops. Only alpha changes: all source RGB samples, even underneath removed paper, are preserved exactly. No resizing, repainting, palette reduction, lighting removal or imagegen edit occurs. Enclosed ivory minerals, cloud and plasma retain their artwork. Superseded packaged celestial art is backed up locally under `raw/celestial/retired/` before removal from the pack.

The planet and all twelve asteroids have matching normal and depth images. Depth is geometric camera-facing surface displacement, with linear red and white nearer. The planet uses an analytic spherical hemisphere; cloud/continent colour and the painted night side do not affect its distance. Asteroids use their silhouette's rounded volume, gentle spatial undulations, and registered crater/scar depressions and geological steps. The rare arch's hole is alpha coverage, not a painted black recess. Per-frame `depth_scale` converts this field to rendered-height units.

Normals derive from the same continuous geometric field before 8-bit depth quantisation: view-space +X right, +Y up, +Z towards the viewer. Nearest-surface padding prevents artificial cliffs at transparent edges. Normal channels use the existing five-effective-bit signed quantisation convention. The emissive sun needs no surface normal/depth maps.

These are authored 2.5D visual approximations with one visible surface, not reconstructed watertight meshes or collision geometry. Geometry does not use albedo luminance, shadows, print hatching or mineral colour as distance.

The extraction receipt, source hashes, installation patches and colour/depth/normal review sheets are written to ignored `raw/celestial/`.

After regeneration, format the new sidecars with `./tools/format/node_modules/.bin/prettier.cmd --write content/base/assets/celestial/*.json` (or the repository formatter).

## Verification

Use the normal production `phoenix_pack` and `phoenix_verify` tools on the base and Test Missions packs. The manually invoked `phoenix_celestial_gallery` target loads the real Rescue map and renders all fourteen materials through `SceneRenderer` in colour, depth, normals and opposite-light passes. It is excluded from ordinary builds and CTest; its captures are review evidence, not shipped-content pixel goldens.

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_celestial_gallery
New-Item -ItemType Directory -Path build/celestial -Force | Out-Null
./build/native/Release/phoenix_pack.exe content/base build/celestial/base.zip
./build/native/Release/phoenix_pack.exe content/test-missions build/celestial/test-missions.zip build/celestial/base.zip
./build/native/Release/phoenix_verify.exe build/celestial/base.zip build/celestial/test-missions.zip
./build/native/Release/phoenix_celestial_gallery.exe build/celestial/base.zip
```

On 31 October 2026, production base and Test Missions packaging and their combined verification passed (55 resolved entity definitions, eight maps, 63 consoles). The manually invoked native OpenGL gallery loaded Rescue and captured all fourteen materials in all four passes; colour and depth captures were inspected. The three existing synthetic cases `system.scenario_world`, `system.scenario-situations` and `system.content-and-packs` passed. Source/output pixel comparison confirmed unchanged RGB arrays and dimensions for all fourteen images; all thirteen depth/normal pairs matched dimensions and had valid normal lengths. Planet centre depth was 245 versus 162 near its limb, with symmetric camera depth across differently painted hemispheres. Captures and logs remain under ignored `build/celestial/`. Browser/WebGL rendering was not rerun for this content change.
