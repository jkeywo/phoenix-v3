# Custom properties migration evidence

Implemented and independently reviewed against `3f736a6` on `codex/custom-properties`, following the [approved plan](custom-properties-migration-plan.md). Automated verification completed on 27 October 2026. This document records executed evidence separately from remaining manual acceptance.

The preceding shared-rules migration and ability transaction consolidation are included in the base. The old attached directory no longer had Git registration; this slice uses the isolated `implementation` checkout within it. Unrelated primary-checkout work is preserved.

## Implemented scope

Typed mod-extensible property definitions, explicit actor/equipment/officer/compartment declarations, native stored owner state, additive attributes and priority-ordered flag/enum contributions use the existing effect contribution entities. Property operations and quantity costs participate in prepared transactions. Ongoing rule and ability costs share the existing work quotation and electrical allocation path, with property inputs and persisted credits. Deferred property-change reactions use the existing bounded rule queue.

Abilities use shared expression requirements and frozen actual-work receipts. Production psychic strain is authored as properties and ordinary officer-lifetime rules; there is no native fatigue scheduler. Existing ability observations carry explicitly selected effective properties to authorised Console users and Controller decision facts. World and portable session schemas advance from 28 to 29.

Exact save continuation exposed sensing record order changing after ECS application churn and restoration. Contact-index inputs now sort by stable entity ID before deriving aligned spatial boxes. Collision shapes and sensing rules are unchanged; exact continuation assertions remain in place.

## Review

Independent correctness and architecture review identified retained request handling, compartment event addresses, logical modifier owner resolution, officer application identity, stale quote comparisons, target rebinding, saved remainder/capture bounds, mixed-funding starvation and survivor caps, and fractional rate-source retirement. These were corrected with synthetic regression cases. Architecture feedback led to sharing compiled property definitions across compilation passes and removing an unused snapshot helper. Source retirement reuses existing native lifetimes rather than adding another registry.

The reviewer rechecked the corrections and the final AI observation, stable contact-index ordering and named-module include changes, reporting PASS with no remaining actionable findings. The final independent review inspected immutable implementation `ceddc3abdc53120c41955baa312f95ff912beb45` against `3f736a6` and the native/threaded evidence logs, confirming PASS. Reviews were read-only and did not repeat builds. Subsequent evidence-document changes do not change the reviewed code.

## Verification status

Windows native and Emscripten threaded builds pass. `scripts/check.ps1 -Target native` passed all 45 CTest integration suites, the native build check and 31 web-launcher cases. Its network stage passed 9/12 cases; the three lobby cases could not open journals because this deeply nested worktree exceeds Windows path limits. A complete `scripts/check-network-service.ps1` rerun passed all 12 cases using `PHOENIX_NATIVE_BUILD_DIR` with a temporary short junction to the same build directory. The original combined command is not reported as a passing full gate.

Twenty native continuation files were produced before the network failure. The remaining psychic and Controller traces were generated explicitly with their native executables' `--trace` modes. The first threaded gate passed every system suite and production preflight, then stopped because those two native reference files were absent. After generating them, `scripts/check.ps1 -Target web -Threaded` passed in full, including production launch/content-refresh preflight and all 22 native/WASM continuation comparisons. No source changes were needed for these gate-environment corrections.

The local toolchain uses the installed Node 24.13.0 executable, including through the scripts' expected SDK launcher path; it does not claim a Node 24.19.0 run. CMake dependency sources are reused from the primary checkout. These local toolchain adaptations are ignored files, not production changes.

The ordinary WebAssembly configuration was built with `scripts/build.ps1 -Target web -Threaded:$false` for content, custom-property, ability, psychic, rule, store-effect, simulation and Workshop-test targets. All seven executable suites passed. `tests/system/base_verifier.mjs` passed real production launch and bundled-content-refresh preflight against the ordinary Workshop-test executable. Six native/ordinary WASM continuation comparisons matched: custom properties, abilities, psychic work, rules, store effects and simulation. The ignored local helper `build/check-properties-ordinary.ps1` records these commands. This is focused ordinary-build evidence, not a claim that the full threaded suite was repeated in ordinary mode.

Standard formatting and `git diff --check` pass. Logs are ignored local artifacts under `build/`:

- `properties-native-final-build-2.log`, `properties-native-gate.log` and `properties-network-gate-short-path.log` record the native results above.
- `properties-threaded-build-2.log`, `properties-threaded-gate.log` and `properties-threaded-gate-2.log` record the threaded build and both gate attempts.
- `properties-ordinary-build.log` and `properties-ordinary-gate.log` record the ordinary configuration and focused checks.
- `properties-final-format-check.log`, `properties-final-format.log` and `properties-completion-format-check.log` record the standard formatter checks, including the final evidence text.

GUI journeys, screen-reader operation, physical devices and performance remain separate manual acceptance. No Linux execution or graphical/device acceptance is claimed. Older saves require a new session under schema 29.
