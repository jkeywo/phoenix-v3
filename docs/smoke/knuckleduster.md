# Knuckleduster Cruiser art integration

31 October 2026. Explicitly requested art preparation and native rendering smoke; not a balance or performance acceptance test.

The production `base.cruiser` uses the approved Knuckleduster front, front-quarter, side, rear-quarter and rear drawings. The opposing three quarters/profile use the renderer's existing horizontal reflection contract. Five independent main turret visuals replace the previous shared turret visual. Gameplay weapon counts, bearings, ammunition, compartments, collision geometry and mechanisms are unchanged.

## Editable source

Run `python scripts/prepare-knuckleduster-assets.py --install` from the repository root. Pillow, NumPy and SciPy perform image manipulation only; this preparation does not call ImageGen or another image-generation service.

`raw/knuckleduster/` contains the five original cream-background drawings; individual hull/turret PNGs for each supplied bearing; six transparent albedo atlases; matching depth/normal maps; six `.sprite.json` import sidecars; canonical material JSON; `registration.json`, `verification.json` and `installation.json`; and `extraction-preview.png`. Ordinary builds consume the tracked copies under `content/base/assets/knuckleduster/`, not ignored raw sources.

Cream removal flood-fills border-connected paper, leaving enclosed ivory armour intact. A source-coordinate caption cutoff excludes labels. Registered polygons separate all visible main turrets; their masks partition the retained source coverage exactly. A saved-file audit puts the exported parts back at their integer source coordinates and compares against the original. **4,111,570 retained source RGB pixels reassemble exactly**: no image resampling, recolouring, palette quantisation, painting or inpainting. Invisible atlas RGB is cleared for lossless compaction; alpha and all visible RGB remain exact. Preview thumbnails are separate review images.

## Depth and attachments

Depth is a hand-registered geometric approximation, not a brightness/shadow conversion or an exact recovered 3D model. It combines a structural core, raised armour slabs, recessed throat, separate cylindrical engine housings, recessed nozzle bowls and missile apertures. No RGB, luminance or painted illumination enters the geometric depth function. Bright exhaust interiors are recessed behind their lips. View-space normals derive from the continuous geometric camera-Z gradient before depth-byte quantisation, with transparent-edge extension to avoid false pits.

All maps share their albedo atlas dimensions. White depth is nearer; normals are view-space +X right, +Y up, +Z toward the viewer. Frame calibration and attachment depth offsets use rendered-height units. The exact registered pivot for each extracted turret is also its hull socket; common source-pixel scale preserves its size. The Cruiser render scale preserves the preceding side-view apparent length, without changing the collision fit.

The five turret sockets are `turret_fore_dorsal`, `turret_mid_dorsal`, `turret_aft_dorsal`, `turret_fore_ventral` and `turret_aft_ventral`. Ventral art is extracted in its actual orientation, so instances use `flip_y: false`. Aft sprite azimuth labels are offset by 180 degrees to select artwork against turret heading rather than hull heading. Every gun can traverse independently. Layer signs supply a small ordering bias; physical camera depth and calibrated offsets determine overlap with the hull. Four exhaust sockets replace the old two, retaining the same propulsion System; the four nozzle-local effect sprites use a smaller presentation scale.

The source drawings do not expose every turret's back. Five occluded mount/bearing combinations reuse the nearest actually painted view of the same mount, recorded explicitly in `registration.json`; their neutral placement is behind the hull. Opposite-side hull art is mirrored, so this is an eight-direction illustrated set, not eight independently authored sides. Extraction does not fabricate hidden hull underpainting. These are limits of the supplied artwork, particularly for arbitrary turret traverse, rather than claims of new pixels or exact multiview geometry.

## Verification and smoke

The preparation audit checks exported RGB reassembly, disjoint coverage, all six eight-bearing sets, map dimensions, atlas rectangles, pivots, attachment bounds and finite depth offsets. Installation compares the Cruiser's non-presentation data before/after.

Native inspection target:

```powershell
./.tools/python/cmake/data/bin/cmake.exe --build build/native --config Release --parallel 1 --target phoenix_knuckleduster_gallery -- /nr:false /p:BuildInParallel=false
./build/native/Release/phoenix_knuckleduster_gallery.exe build/native/base.zip
```

The real content loader, World scene projection and OpenGL scene renderer produce seven galleries in `build/knuckleduster/`: `bearings.png`, `normals.png`, `depth.png`, `traverse-90.png`, `traverse-180.png`, `traverse-270.png` and `engines.png`. Each contains eight camera bearings. The probe requires five independent turret attachments and resolves all five gun sockets plus four exhaust sockets at every bearing. The neutral, depth, traverse and thrust captures were visually inspected on the NVIDIA GeForce RTX 5090 Laptop GPU, OpenGL 3.3.

Initial full working-source packaging encountered an unrelated rescue portrait being authored concurrently. A snapshot of the previously verified base plus only the Cruiser art changes verified and rendered successfully. Once those portrait files were present, the complete current source base also packaged successfully. No unrelated portrait, gameplay or transfer-limit work was included in this art change. Browser rendering and physical multi-display operation were not exercised by this native art smoke.

The final native build, `base_pack`, `test_missions_pack`, `phoenix_verify` and gallery targets passed. The final combined pack verification resolved **55 entities, eight maps and 63 consoles**. `native.exe --build-check` passed, and the seven-gallery inspection passed again against the complete rebuilt production base. The native executable's local `assets/` copies and client companions were updated by the ordinary pack targets.
