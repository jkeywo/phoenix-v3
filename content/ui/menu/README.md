# Native menu artwork

The button and dialog backing are promoted unchanged from the local `raw/` HUD study, together with their version 1 `.sprite.json` sidecars and schema. Runtime and packaged builds use this directory, with no dependency on the ignored scratch folder.

`MenuSkin` reads each sidecar at UI startup: relative sprite filename, source dimensions, source-pixel nine-slice borders and optional `label_center`. It rejects invalid bounds/paths and verifies the actual PNG dimensions. The renderer generates RmlUi spritesheets and nine-patch decorators from that data. Display scale is a UI choice (button 0.075, backing 0.12); corners retain that size while centre/edges stretch. Live labels follow the marker through the same piecewise layout, including corner compression on small controls. Labels are localised text and remain separate from images, navigation and UIA semantics.

`settings.png` and `background.png` were generated on 18 October 2026 with the built-in image_gen tool. They match the HUD study's battered ivory metal, orange inserts, heavy ink, scratches and halftones. The settings image retains its alpha; the opaque background uses aspect-preserving cover fitting. The gear is decorative inside a labelled Settings control. Full prompts and transparency choices are recorded in [generation.json](generation.json).

Button hover, keyboard focus, pressed, selected and disabled states use live colour/border/opacity styling. They do not require baked text or separate image copies. Popups use the existing helm backing through its own sidecar. Assets are copied into native `assets/menu/` and the shared WASM asset bundle by CMake.
