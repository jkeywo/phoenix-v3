# Launch bases and model scene verification

20 October 2026. Implementation contract: [base mods](../base-mods.md).

Automated correctness evidence:

- Native synthetic content and menu-library integration pass, including locked roots, parent chains/cycles, separate base and local-profile preferences, model-family Workshop creation, restricted Console projection and cross-base/revision save rejection.
- The 14 browser launcher cases pass. Native and WASM content round trips pass, including independent synthetic panorama/cube-strip references and malformed image rejection.
- Production, `test` and `test-3d` archives verify independently. `development_packs` is explicitly invoked; normal builds do not require those archives.
- All eight local real-network service cases pass, including fleet continuation, Console routing, content admission and readiness.
- Native build check passes. Browser server/client/Workshop compile in the isolated `build/base-web` tree, using the pinned toolchain and dependencies.
- Production staging in `build/base-public-verification` passes `scripts/verify-deployment.mjs`: production allowlist only, with neither development archive, its dedicated models, nor V2 skybox advertised/preloaded.

Explicit graphical evidence, separate from correctness checks:

- Native `test` and `test-3d` launches render through the existing GUI. Final model capture: `build/model-sky-native-smoke.png`; process exits successfully after 100 frames.
- Browser `?base=test-3d.zip` reaches the lobby and scene with the base URL preserved through the role launch. The V2 Courier, asteroids, procedural star, V2 skybox and horizon grid are visible. No scene shader errors were reported in the inspected browser logs.
- The V2 skybox is a per-face 512-pixel derivative of its original six-face strip, within ZIP/DAT bounds. Source/derivative hashes and bake instructions are recorded with the development content. V2 remains unchanged.

Remaining verification limits:

- Full native regression reports 21/22 passing; `system.gameplay_flight` fails its existing `Notice: Supply 7` Console announcement assertion. This is separate from the focused content/root checks above.
- Repository formatting check reports drift in `src/presentation/messages.hpp`, `tests/manual/messages_ui.cpp` and `content/ui/messages/portraits.json`. Those unrelated message changes are preserved.
- The external signalling service failed during the browser smoke. The browser scene remained at tick zero after launch, so this capture does not establish remote participation, live flight, thrust-light behaviour or save/resume through the browser GUI. These journeys still require explicit verification. Synthetic save compatibility passes.
- Physical devices, screen readers, six-display performance and final artistic/tuning acceptance are not established by these captures.
