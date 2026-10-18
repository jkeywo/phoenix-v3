# Native menu artwork

The button and dialog backing are promoted unchanged from the local `raw/` HUD study, together with their version 1 `.sprite.json` sidecars and schema. Runtime and packaged builds use this directory, with no dependency on the ignored scratch folder.

`MenuSkin` reads each sidecar at UI startup: relative sprite filename, source dimensions, source-pixel nine-slice borders and optional `label_center`. It rejects invalid bounds/paths and verifies the actual PNG dimensions. The renderer generates RmlUi spritesheets and nine-patch decorators from that data. Display scale is a UI choice (button 0.075, backing 0.12); corners retain that size while centre/edges stretch. Live labels follow the marker through the same piecewise layout, including corner compression on small controls. Labels are localised text and remain separate from images, navigation and UIA semantics.

`settings.png` and `background.png` were generated on 18 October 2026 with the built-in image_gen tool. They match the HUD study's battered ivory metal, orange inserts, heavy ink, scratches and halftones. The settings image retains its alpha; the opaque background uses aspect-preserving cover fitting. The gear is decorative inside a labelled Settings control. Full prompts and transparency choices are recorded in [generation.json](generation.json).

Button hover, keyboard focus, pressed, selected and disabled states use live colour/border/opacity styling. They do not require baked text or separate image copies. Popups use the existing helm backing through its own sidecar. Assets are copied into native `assets/menu/` and the shared WASM asset bundle by CMake.

## Moving space and CRT terminals

The active menu draws `space-panorama.png` behind the alpha window of `cockpit-frame.png`. The original opaque `background.png` is retained as source artwork. A pair of alternating mirrored panorama copies repeats continuously; matching edge texels avoid a hard vertical seam. The cycle takes twelve minutes (about six pixels per second at 720p). Reduced motion pauses at the current position. The fixed cockpit, controls and CRT do not pan. Native clips the scene to its configured menu region; HTML uses compositor transforms and pauses with the same setting or the browser's reduced-motion preference.

`crt-terminal.png` is the reusable front-facing television, ceiling stalk and dense tangle of loose/coiled cables. The upper-right profile display is 264px wide, 10% larger than its initial 240px layout. Its avatar and username are live content from the persisted player profile; a question mark occupies the avatar area until an image is chosen. Profile readouts remain accessible text.

The lobby reuses the sprite in its invitation column. Its long upper stalk is cropped there to keep the complete QR code visible at entry. QR modules use integer pixel scaling, nearest filtering and their original quiet border; no CRT noise or distortion is applied to the code.

[menu-scene.json](menu-scene.json) declares image dimensions, panorama timing and CRT screen/crop coordinates in source pixels. Native validates image dimensions and bounds; HTML derives the same screen aperture and motion properties from the metadata. Keep authored geometry separate from display scale. These three assets were made with built-in imagegen; prompts, transparency choices and the later cabling edit are in [scene-generation.json](scene-generation.json).

## Nebula flashes

`nebula-flash-mask.png` selects the brighter dust bands while excluding empty space and the moon. It is an alpha mask, generated from the original panorama and the user's annotated guide; [flash-generation.json](flash-generation.json) retains the prompt. Neither the green annotations nor a painted lightning bolt appear in the runtime. The renderer adds a masked copy of the original panorama at up to 90% strength, preserving its texture and matching its pan, scale and mirrored repeats. Native uses additive texture blending; HTML uses a masked `plus-lighter` layer. The cockpit, buttons, title and CRT remain outside this effect.

Scene metadata defines variable 20–30 second start-to-start intervals and two pulses: ramp up 0.1s, hold 0.1s, ramp down 0.1s, dark gap 0.3s, ramp up 0.1s, hold 0.3s, ramp down 0.1s. The burst lasts 1.1s. Its clock and private cosmetic PRNG never touch gameplay ticks or random state. Reduced motion disables flashes; the browser also cancels them while hidden and restarts with a fresh quiet interval when shown again.
