# Rescue opening scenery

`planet.png` is a transparent RGBA PNG generated with OpenAI imagegen for Phoenix V3 on 23 October 2026. The existing `assets/comic/asteroid-albedo.png` supplied its illustrated reference: ink outlines, stippled shading, muted ochre, ivory and teal.

The planet is one spherical illustrated cutout. The starbase is a strict side elevation, regenerated following the requested correction from the earlier three-quarter view. Only the side-on version is packaged. Both are authored as single camera-facing billboards in `definitions.json`; no directional sprite selection or orbital simulation is involved.

Generated planet source identifier: `exec-7efff45f-2bba-432e-a39e-c783389622c7`.

## Circular starbase replacement, 30 October 2026

The current station comes from `docs/art/phoenix-starbase-side-v1.png`, the approved circular side elevation with an exaggerated asymmetric spire. `scripts/prepare-starbase-sprite.py` removes the ivory paper using Pillow/numpy/scipy, with no image generation, resizing, repainting or RGB changes. Only alpha is changed; all RGB samples, including transparent pixels, remain byte-identical to that source. Border-connected paper and four visually identified crane/brace openings become transparent; enclosed ivory armour remains opaque.

The editable set is in ignored `raw/starbase/`: `starbase.png`, `starbase.sprite.json`, `starbase-depth.png`, `starbase-normal.png`, an extraction receipt and a dark-background review composite. The production PNGs are byte-identical copies of the raw files. Run `python scripts/prepare-starbase-sprite.py` to reproduce this set and update its production material, sidecar and entity render block. Use this dedicated preparation step after any broad legacy relief rebake.

Depth is an approximate geometric model, independent of albedo brightness and painted shadows: the visible near half of a horizontal torus, a central upright behind it, projecting control platforms and spherical rim tanks. Linear red encodes camera-facing depth, white nearer; normals use view-space +X right, +Y up, +Z toward the viewer and derive from that same field. The 1600-source-pixel depth span gives `depth_scale: 1.5625` for the 1024-pixel-high frame. This is a calibrated proxy, not an exact recovered mesh or a shadow mask.

The full source remains 1536 by 1024. Pivot `[768, 744]` registers the ring centre, with the spire above it. Render height 266.6666666666667 metres retains the former 400-metre frame width; simulation collision and docking rules are unchanged. Existing inherited station definitions use the replacement through the same asset reference.
