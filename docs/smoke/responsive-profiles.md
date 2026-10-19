# Responsive menu and console profiles

19 October 2026: native Release and all three Emscripten roles built successfully. Nine launcher checks passed, including portrait/landscape fitting and inverse canvas pointer mapping. The native menu-library, input-and-semantics, local-session and fleet-setup integration checks passed.

The manually triggered `--lobby-regions-smoke` passed with two synthetic display regions, including opening the console TV, editing its own name, choosing a portrait, saving the profile, excluding Leave/Back, readiness/countdown, launch, settings, save and quit. Profile selection and inline mod-pack controls were visually inspected. These virtual regions do not establish physical multi-monitor or touch-device acceptance.

The HTML menu and profile dialog were inspected in 844 by 390 and 390 by 844 browser viewports. Both fit without scrolling; portrait mode rotates the full stage and dialog. This is browser viewport evidence, not a physical phone or mobile screen-reader test. Large custom pack/catalog lists retain overflow as a fallback.

The twelve generated portraits retain the rugged comic faces, add four younger adults, and replace armour with navy cloth uniforms. Generation/edit prompts are recorded in `content/ui/menu/avatars.md`.
