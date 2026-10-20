# Launch bases and model scene verification

20 October 2026. Implementation contract: [base mods](../base-mods.md).

Automated correctness evidence:

- Native synthetic content and menu-library integration pass, including locked roots, parent chains/cycles, separate base and local-profile preferences, model-family Workshop creation, restricted Console projection and cross-base/revision save rejection.
- The 14 browser launcher cases pass. Native and WASM content round trips pass, including independent synthetic panorama/cube-strip references and malformed image rejection.
- Production, `test` and `test-3d` archives verify independently. Both development bases also package and verify as compiled DAT archives. `development_packs` is explicitly invoked; normal builds do not require those archives.
- All eight local real-network service cases pass, including fleet continuation, Console routing, content admission and readiness.
- Native build check passes. Browser server/client/Workshop compile in the isolated `build/base-web` tree, using the pinned toolchain and dependencies.
- Production staging in `build/base-public-verification` passes `scripts/verify-deployment.mjs`: production allowlist only, with neither development archive, its dedicated models, nor V2 skybox advertised/preloaded.

Explicit graphical evidence, separate from correctness checks:

- Native `test` and `test-3d` launches render through the existing GUI. Final model capture: `build/model-sky-native-smoke.png`; process exits successfully after 100 frames.
- Browser `?base=test-3d.zip` reaches the lobby and scene with the base URL preserved through the role launch. The V2 Courier, asteroids, procedural star, V2 skybox and horizon grid are visible. No scene shader errors were reported in the inspected browser logs.
- The V2 skybox is a per-face 512-pixel derivative of its original six-face strip, within ZIP/DAT bounds. Source/derivative hashes and bake instructions are recorded with the development content. V2 remains unchanged.

Remaining verification limits:

- Full native regression reports 21/22 passing; WASM reports 18/19 passing with matching native/WASM continuation traces. The same flight case fails on both platforms; `system.gameplay_flight` fails its existing `Notice: Supply 7` Console announcement assertion. This is separate from the focused content/root checks above.
- Repository formatting check reports drift in `src/presentation/messages.hpp`, `tests/manual/messages_ui.cpp` and `content/ui/messages/portraits.json`. Those unrelated message changes are preserved.
- The external signalling service failed during the browser smoke. The browser scene remained at tick zero after launch, so this capture does not establish remote participation, live flight, thrust-light behaviour or save/resume through the browser GUI. These journeys still require explicit verification. Synthetic save compatibility passes.
- Physical devices, screen readers, six-display performance and final artistic/tuning acceptance are not established by these captures.

## Review follow-up, 20 October 2026

An independent agent reviewed the implementation against the original plan. The following corrections supersede the earlier regression and tick-zero limitations:

- Browser launch now verifies root manifests, definitions, images and models through the production C++ loader before displaying the menu. Forged preview data cannot select launch content. Invalid explicit URLs display a launch error.
- Browser persistence merges the current base/profile inside the IndexedDB transaction, retaining concurrent tabs' settings. Return URLs preserve base/profile. Disabled descendants cannot be orphaned by reordering/removing parents; duplicate IDs cannot replace the selected root.
- Native installed-tree validation includes disabled packs. Workshop preserves existing parent metadata, locks roots and presents nested mods with an explicit valid-parent selector.
- Original default-profile preferences/saves migrate through a narrow historical Phoenix-root adapter; saves still require their original content revision. Updating the same base ID preserves its preferences and child selection.
- GLB checks now respect the active scene, generate missing normals, validate transformed normals, bound repeated embedded textures and accept the implemented emissive-strength extension. Synthetic ZIP/DAT/Workshop round trips and dynamically spawned model ships exercise production paths.
- Model rendering now shares volumetric fog with sprite rendering and resolves attachments through the same transform as visible geometry.
- The flight regression expected an obsolete unattributed message. Both platforms pass with the existing `Helm: Notice: Supply 7` contract.
- Live browser simulation was advancing: the tick-zero report was cached accessibility status when Console mode bypassed full GameScreen preparation. Telemetry now refreshes independently before Console announcements. The explicit native camera journey verifies that committed ticks change the accessible status in Console mode.
- A second real browser participant exposed a restricted-Console projection error: resolved station preferences still referenced stations omitted from the bundle. Projected preferences now retain only included stations, preserving order. A synthetic two-station restricted round trip reproduces and guards the failure.

Correctness results: native regression 22/22 at the review baseline, WASM 19/19 including the final Console projection fix, browser launcher 19/19, production WASM launch-preflight fixtures, matching native/WASM continuation trace, and all eight local real-network service cases (including intentional base-ID/revision mismatch refusals). The final native focused content/projection check passes. Explicit independent development ZIP/DAT pack builds pass. The allowlisted staging directory `build/review-public-final` passes deployment exclusion inspection; a test-3d archive renamed to `base.zip` is also deliberately rejected. No deployment was published.

Explicit graphical evidence: native and browser sprite camera journeys pass all 16 camera renders and input/modal checks (`build/review-camera-evidence`, `build/review-sprite-browser.png`); native model launch exits successfully (`build/review-model-final.png`). Browser model session saved at tick 621, returned to its base/profile menu, listed that save and resumed from the same committed tick through a loopback coordinator. A separate browser player was approved, claimed Helm and commanded forward flight. The client reported tick 987 / position 154 m / speed 14 m/s; the host reported tick 998 / position 162 m / speed 14 m/s, durable majority ready. Third-person camera selection also reached its selected state. Screenshots: `build/review-model-browser-flight.png`, `build/review-helm-browser-flight.png`. Model-base Workshop opens its locked root, skybox assignment and model preview with engine plume (`build/review-workshop-model-preview.png`); no inspected browser shader errors. These establish real browser persistence and remote play rather than only synthetic compatibility checks.

Unrelated concurrent bridge UI changes remain outside this review's implementation scope. A final prescribed native rebuild after those concurrent content edits stopped while generating production `base.zip`, with `Controller tree: invalid System observation binding` (`build/review-final-native-gate.log`). This does not supersede the earlier passing regression or final focused content check, but the current combined checkout cannot be claimed to have a green native gate. Formatting is checked on review-owned files; whole-repository audit still reports unrelated drift in `src/presentation/messages.hpp`, `tests/manual/messages_ui.cpp`, `tests/manual/native_menu.hpp` and `content/ui/messages/portraits.json`. Those files are preserved. The physical-device, screen-reader, six-display performance and final art/tuning limits above remain. Authored test bases remain optional smoke evidence rather than mandatory tuning goldens.
