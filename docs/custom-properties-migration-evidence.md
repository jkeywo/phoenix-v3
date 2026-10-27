# Custom properties migration evidence

Implementation against `3f736a6` on `codex/custom-properties`, following the [approved plan](custom-properties-migration-plan.md). This document records executed evidence separately from implementation claims and manual acceptance. Final validation is in progress on 27 October 2026.

The preceding shared-rules migration and ability transaction consolidation are included in the base. The old attached directory no longer had Git registration; this slice uses the isolated `implementation` checkout within it. Unrelated primary-checkout work is preserved.

## Implemented scope

Typed mod-extensible property definitions, explicit actor/equipment/officer/compartment declarations, native stored owner state, additive attributes and priority-ordered flag/enum contributions use the existing effect contribution entities. Property operations and quantity costs participate in prepared transactions. Ongoing rule and ability costs share the existing work quotation and electrical allocation path, with property inputs and persisted credits. Deferred property-change reactions use the existing bounded rule queue.

Abilities use shared expression requirements and frozen actual-work receipts. Production psychic strain is authored as properties and ordinary officer-lifetime rules; there is no native fatigue scheduler. Existing ability observations carry explicitly selected effective properties to authorised Console users and Controller decision facts. World and portable session schemas advance from 28 to 29.

## Review

Independent correctness and architecture review identified retained request handling, compartment event addresses, logical modifier owner resolution, officer application identity, stale quote comparisons, target rebinding, saved remainder/capture bounds, mixed-funding starvation and survivor caps, and fractional rate-source retirement. These were corrected with synthetic regression cases. Architecture feedback led to sharing compiled property definitions across compilation passes and removing an unused snapshot helper. Source retirement reuses existing native lifetimes rather than adding another registry.

The reviewer rechecked the corrections and the final AI observation, stable contact-index ordering and named-module include changes, reporting PASS with no remaining actionable findings. Frozen-revision confirmation follows the final gates. Reviews were read-only and did not repeat builds.

## Verification status

Windows native and Emscripten threaded builds pass. `scripts/check.ps1 -Target native` passed all 45 CTest integration suites, the native build check and 31 web-launcher cases. Its network stage passed 9/12 cases; the three lobby cases could not open journals because this deeply nested worktree exceeds Windows path limits. A complete `scripts/check-network-service.ps1` rerun passed all 12 cases using `PHOENIX_NATIVE_BUILD_DIR` with a temporary short junction to the same build directory. The original combined command is not reported as a passing full gate.

Twenty native continuation files were produced before the network failure. The remaining psychic and Controller traces were generated explicitly with their native executables' `--trace` modes. The first threaded gate passed every system suite and production preflight, then stopped because those two native reference files were absent. After generating them, `scripts/check.ps1 -Target web -Threaded` passed in full, including production launch/content-refresh preflight and all 22 native/WASM continuation comparisons. No source changes were needed for these gate-environment corrections.

The local toolchain uses the installed Node 24.13.0 executable, including through the scripts' expected SDK launcher path; it does not claim a Node 24.19.0 run. CMake dependency sources are reused from the primary checkout. These local toolchain adaptations are ignored files, not production changes.

Ordinary WebAssembly checks and final documentation/format checks remain pending. Logs are ignored local artifacts under `build/`:

- `properties-native-final-build-2.log`, `properties-native-gate.log` and `properties-network-gate-short-path.log` record the native results above.
- `properties-threaded-build-2.log`, `properties-threaded-gate.log` and `properties-threaded-gate-2.log` record the threaded build and both gate attempts.
- `properties-final-format-check.log` and `properties-final-format.log` record the standard formatter checks; the final evidence text will receive one final check.

GUI journeys, screen-reader operation, physical devices and performance remain separate manual acceptance.
