# Torpedo sprite draft — 23 October 2026

The combat missile uses five near-level drawings: bow, port bow, port, port stern and stern. The remaining three bearings reuse the port rectangles with `mirror_x`. Both the canonical `base.combat-missile` material and the adjacent `missile.sprite.json` describe all eight bearings. Forward vectors describe the displayed nose direction, including the mirrored X direction, because the current velocity-alignment renderer consumes them directly.

The atlas includes a static blue-white/amber exhaust flame; the bow view hides it behind the hull and the stern view shows it end-on. The existing smoke trail and projected-velocity rotation remain. This is an artwork iteration, without new pitch selection, roll simulation, emission maps or exhaust animation. Pivots follow the hull rather than the flame; render scale 1 keeps the broadside hull close to its previous displayed length (about 2.5 metres).

Generated with the built-in image tool, using the previous atlas as the design reference and a first draft as the final edit input. The selected transparent PNG is `content/base/assets/combat/missile-atlas.png`. No programmatic recolouring or background removal was applied.

## Verification

The native production packager accepted the sprite update over the existing bundled base pack. The current verifier accepted that result together with the bundled Test Missions pack: 35 entities, eight maps and 20 consoles. Sprite metadata and this note pass the repository Prettier configuration. The PNG has an alpha channel and the five source regions were inspected. No in-game visual acceptance is claimed; this draft is for the user's flight review.

The full working-tree repack encountered an unrelated `scenario: invalid transition event binding` in concurrent scenario work. The local preview therefore updates only the missile in the existing native bundle, with its previous archive retained at `build/torpedo-preview/base-before-sprites.zip`. Source definitions retain the same missile update for the next full build.

## Final generation prompt

```text
Use case: precise-object-edit. Edit this fictional spaceship game's torpedo sprite atlas. Keep the cream/red/black inked-comic design and five-view vertical layout, but correct two specific things: true end-on front/back geometry and BAKED-IN ENGINE EXHAUST. This is an isolated nonviolent fictional game asset, no action, targets, people, injury, explosions or real construction information.
Canvas 1024 x 1536, real transparent background. Exactly FIVE sprites, each centered within one equal-height horizontal cell, with generous completely empty transparent gutters. Entire engine flame must fit inside its own cell. Fixed identical physical scale across all views, same hull and FOUR fins, consistent dimensions. Neutral lighting for horizontal mirroring. No background glow, shadows, labels, text or extra views.
Camera is now EXACTLY LEVEL with the horizontal torpedo axis, ZERO elevation; NOT an overhead view. Top to bottom:
1. TRUE FRONT END-ON: camera sits precisely on the longitudinal centerline looking at nose. Circular red conical nose face with a small centered metallic tip, circular ivory shoulder around it, four fins at cardinal directions behind it. NO long body visible above or below, no tail poking up. Short compact symmetric silhouette. The engine and flame are fully hidden behind the body from this angle.
2. FRONT-LEFT QUARTER: nose toward viewer and left at 45 degrees yaw. Left-pointing axis exactly horizontal in the image. Modest foreshortening. Small jet extends rightward behind rear nozzle, partly occluded by hull as physically appropriate.
3. EXACT LEFT SIDE PROFILE: red nose at left, rear engine at right. Long horizontal body. Baked short tapered exhaust flame extends to RIGHT from nozzle; bright pale blue-white core with soft amber/orange outer flame. Flame about 20 percent of hull length, crisp painterly sprite edges and transparent outside, no smoke.
4. REAR-LEFT QUARTER: nose points away from viewer and left, rear nozzle at right towards viewer. Axis horizontal. Baked same compact exhaust emerging from nozzle toward viewer-right, correctly foreshortened.
5. TRUE REAR END-ON: camera precisely on longitudinal axis behind engine. Symmetric circular dark nozzle with central bright blue-white/amber engine flame seen directly END-ON, four fins at cardinal directions. NO long body visible above or below, no red nose visible, no sideways plume. Compact symmetric silhouette same approximate diameter as front view.
Do not tilt front/back objects up/down to show body. Do not draw a long vertical missile in either end-on cell. Preserve weathered hand-inked comic rendering but prioritize coherent model geometry and readable small sprites.
```
