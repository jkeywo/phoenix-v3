# Viewscreen dust and reference grid

V3 ports the **final V2 mote renderer**, rather than the older layered dust design whose configuration remained in V2 after its renderer was retired. The shared C++ renderer supplies the same effects on native OpenGL 3.3 and browser WebGL 2. They are presentation only: they add no simulation components, random numbers, authoritative state, save-state fields or network observations.

## Behaviour and controls

The Viewscreen follows the own ship's translation by default. **Follow ship** switches between following and a fixed camera. Orbit and zoom remain available; panning while following adds an offset. Switching following back on clears that offset and recentres the ship. Heading does not rotate the camera automatically.

Motes occupy a bounded, camera-centred volume. Their anchors stay fixed in world space, wrapping as the camera travels. Actual planar velocity determines projected streak direction and length, including reverse or lateral movement; heading or requested throttle cannot substitute for velocity. At rest the final V2 field retains its fixed population. Three texture/brightness/size classes share batched rendering. Motes fade away from the screen centre and before its edges, using the actual Viewscreen region rather than the full desktop.

The reference grid lies on the world XZ plane, with positive simulation Y mapping to negative render Z. Its patch follows the own ship while the lines remain fixed at world-coordinate multiples. Minor and major lines use pixel widths and alpha blending; the radial fade hides the moving patch boundary. It is visible from either side, shares scene depth occlusion and neither casts shadows nor receives lighting. Renderer rebasing preserves its lattice phase.

**Settings** offers **Dust motes**, **Reference grid** and **Reduced motion**. The first two default on; they can suppress authored effects without changing the pack. Reduced motion hides both aids while preserving their switch values. Native settings persist in the existing menu index; the browser launcher's existing IndexedDB menu persistence carries the same fields. Older indexes default the new switches on. Legacy direct browser launches without a menu library retain display flags in `phoenix-display-preferences-v1` local storage, following the OS motion preference initially. These endpoint choices do not travel with runtime saves or other players.

## Authoring

Maps optionally declare `presentation.dust`. Shared values apply to both platforms; optional `native` and `web` objects override individual fields. An omitted dust configuration uses the defaults below. All settings are available under **Dust motes** in Workshop's lighting/sprite preview.

| Field                                                 | Default                                                                                                                                              |
| ----------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `enabled`, `count`                                    | `true`, `440` (0–10,000)                                                                                                                             |
| `half_width`, `depth`                                 | `30`, `100` metres; positive                                                                                                                         |
| `width`, `streak_per_speed`                           | `0.08` metres, `0.25` per metre/second                                                                                                               |
| `tint`, `brightness`                                  | `[0.7, 0.8, 1]`, `[0.7, 0.55, 0.4]`                                                                                                                  |
| `opacity`                                             | `0.45`                                                                                                                                               |
| `textures`                                            | Three empty strings select the bundled streak-head, soft-streak and compact-core PNGs respectively. Each slot may instead name a pack PNG reference. |
| `centre_fade_inner`, `centre_fade_outer`, `edge_fade` | `0.12`, `0.38`, `0.15` in normalised viewport coordinates                                                                                            |

The three size multipliers are 1, 1.5 and 2. Streak length is `width × size multiplier × (1 + speed × streak_per_speed)`. Colour components and opacity are 0–1; brightness is 0–16. Centre outer radius must exceed inner radius and cannot exceed 2; edge fade is 0–1. Numeric values must be finite and bounded. Custom PNGs use ordinary production asset verification. The built-in textures and their source hashes are recorded in `content/pfx/README.md`.

Ships opt in with `render.reference_grid: {}`. Absence means no grid. Only the own ship's table is read; NPC tables do not produce extra patches. Workshop's entity Properties exposes **Reference grid**, including an author/remove switch.

| Field                                        | Default                                                                                                                 |
| -------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| `minor_spacing`, `major_spacing`             | `10`, `50` metres                                                                                                       |
| `minor_colour`, `major_colour`               | `[0.45,0.62,0.78,0.10]`, `[0.55,0.74,0.90,0.20]`                                                                        |
| `minor_line_width_px`, `major_line_width_px` | `1`, `1.6` pixels                                                                                                       |
| `opacity`                                    | `1`                                                                                                                     |
| `patch_radius`, `fade_band`, `fade_exponent` | `400` metres, `250` metres, `2.5`                                                                                       |
| `plane_y`                                    | `-0.5` metres                                                                                                           |
| `first_person_plane_y`                       | Optional, preserved for future first-person cameras; unused by the current orbit view. The training ship authors `-10`. |

Major spacing must be a whole multiple of minor spacing. Spacing, widths, radius and exponent must be positive; the fade band is 0–radius and opacity/colours are 0–1. All numbers must be finite and bounded. Both configurations follow existing pack inheritance/explicit override rules without changing definitions schema 3. The training Courier opts into the calibrated V2 grid; training maps enable default dust. Other ships retain their existing opt-in choice.

The preview's speed, motion heading and X/Y controls animate presentation-only movement through the same renderer. Reset stops that movement and returns to the authored origin. Content is reverified only when its definitions change; moving the preview does not reverify the pack or rebuild scene resources every frame.

## Verification — 18 October 2026

Synthetic production-loader cases cover defaults, platform overrides, inherited grids, invalid settings, custom PNG references and Workshop ZIP round trips. Preference integration covers persistence, old indexes and semantic admission. The complete repository gate passed: twelve native integration cases, WASM checks, matching native/WASM continuation and seven real signalling/network journeys.

The explicitly invoked `phoenix_comic_probe` passed on native OpenGL and actual browser WebGL 2. Its synthetic cases cover stationary/moving/reverse/lateral streaks, visible grid rendering, reduced motion and region clipping. It asserts lattice-phase preservation after a large whole-cell translation and a changed pattern after a half-cell translation, and records below-plane and offscreen-preview captures. These are manual graphical evidence, not production-content goldens or a frame-rate guarantee. Commands remain those in `comic-rendering.md`; native standalone and browser builds of the probe include their mote textures.

The real browser launcher was used to start `training.navigation` with the Courier, claim its Console, increase speed and return to the Viewscreen. The ship remained centred as position changed and the grid moved underneath it; follow mode was toggled off and on. Launcher settings survived reload, and runtime keyboard controls disabled both aids. Captures and logs are ignored under `build/`, including `navigation-browser.jpg`, `native/Release/navigation-*-smoke.png`, `dust-integration.log` and `native/dust-graphics.log`. Screen-reader usability and physical six-display performance remain separate acceptance work.

The native menu smoke passed with the new switches, keyboard traversal and programmatic Windows UIA checks. Its navigation-aid actions are deliberately issued on separate rendered frames, respecting the menu's stale-action invalidation. The native Workshop loaded the updated pack. Browser Workshop also imported it, selected the Courier, exposed the calibrated tuning tables and animated the live preview at 14 m/s. Its preview capture is `build/navigation-workshop-browser.jpg`; the final complete gate is `build/dust-final-gate.log` and the native menu journey is `build/dust-menu-final.log`.
