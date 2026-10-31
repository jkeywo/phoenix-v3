# Pack Mule freighter and Morrow

Morrow in Rescue under threat uses the independent `base.pack-mule` entity. It does not extend Courier or use its sprite, turret models, weapon controllers, magazines, missile tubes, beam, PDC or weapon-power circuit. The freighter retains ordinary propulsion, steering, communications, sensors, repair teams, defensive shields, FTL, docking and cargo handling. Its civilian power assembly has main and shield stores only. Rescue damage, casualties, dialogue, occupation, boarding and departure remain scenario-owned. Removed gunnery duties leave their officers aboard as unassigned deck crew; they are not deleted from the rescue roster.

## Artwork and raw files

`raw/pack-mule/` contains the transparent individual views, their normal/depth images, the combined atlas, `pack-mule.sprite.json`, canonical material, geometry registration, extraction receipt and review preview. Raw remains ignored, as with the other editable source art. Runtime copies live in `content/base/assets/pack-mule/` so normal builds do not require raw inputs or Python imaging packages.

The atlas stores five unique views and declares eight bearings using the existing `mirror_x` contract. Frame rectangles have different heights to register the same visible vertical extent without resizing any source image. Albedo processing clears alpha for border-connected cream paper, explicitly registered gantry openings and captions, then crops and places pixels using integer translations. It never resamples, recolours, paints, decontaminates edges or invokes image generation. Retained RGB pixels, including the original ink and wear, match the source images exactly. Existing transparent 45-degree views are copied without changing their alpha.

Run explicitly:

```powershell
python scripts/prepare-pack-mule-assets.py
```

This prepares raw and tracked materials only. `scripts/install-pack-mule-definition.py` records the one-time fitting migration; reruns refresh the freighter material and its own console layouts. It is not part of an ordinary build.

## Camera depth and normals

Depth uses registered geometric surface proxies, not greyscale, shadows or emission. With bow-positive longitudinal coordinate F, port-positive coordinate P and camera azimuth a, camera-facing displacement is `Z = F*cos(a) + P*sin(a)`. Cargo faces are planes with small edge bevels, the cab is separately registered, the narrow gantry has its own placement and the stern engine has a rounded barrel and recessed nozzle. The bright nozzle core is farther from the camera than the nozzle lip. Normals are gradients of that continuous geometric field in view space (+X right, +Y up, +Z toward camera). Per-frame calibration and engine attachment offsets use the renderer's existing depth convention.

These are authored 2.5D approximations of the visible surfaces, not a recovered watertight mesh. Painted panel details do not become false depth relief. Hidden surfaces and the source illustrations' imperfect cross-view geometry cannot be reconstructed by pixel-preserving image manipulation.

## Verification

31 October 2026: the production packager accepted the live base pack, and the verifier resolved Morrow as Pack Mule with no weapon, turret or magazine systems. Native content and transfer system checks passed. A manually invoked native OpenGL gallery loaded the real rescue scenario and inspected Morrow's eight bearings, depth, normals and stern exhaust through the shared scene renderer. Captures and command logs are under ignored `build/pack-mule*`. This does not claim a new full rescue playthrough or browser graphical run.

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_pack_mule_gallery
./build/native/Release/phoenix_pack_mule_gallery.exe build/native/base.zip
```

Lossless directional art and launcher metadata require larger bounded containers: 128 MiB archived per pack, 256 MiB expanded source and 256 MiB total ordered transfer. The existing ZIP/DAT compression, lossless image encoding, 16 MiB source-entry limit and 16-megapixel image limit are unchanged.
