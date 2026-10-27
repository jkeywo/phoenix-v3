# Custom properties migration evidence

Implemented and independently reviewed against `3f736a6` on `codex/custom-properties`, following the [approved plan](custom-properties-migration-plan.md). Automated verification completed on 27 October 2026. This document records executed evidence separately from remaining manual acceptance.

The preceding shared-rules migration and ability transaction consolidation are included in the base. The old attached directory no longer had Git registration; this slice uses the isolated `implementation` checkout within it. Unrelated primary-checkout work is preserved.

## Implemented scope

Typed mod-extensible property definitions, explicit actor/equipment/officer/compartment declarations, native stored owner state, additive attributes and priority-ordered flag/enum contributions use the existing effect contribution entities. Property operations and quantity costs participate in prepared transactions. Ongoing rule and ability costs share the existing work quotation and electrical allocation path, with property inputs and persisted credits. Deferred property-change reactions use the existing bounded rule queue.

Abilities use shared expression requirements and frozen actual-work receipts. Production psychic strain is authored as properties and ordinary officer-lifetime rules; there is no native fatigue scheduler. Existing ability observations carry explicitly selected effective properties to authorised Console users and Controller decision facts. World and portable session schemas advance from 28 to 29.

Exact save continuation exposed sensing record order changing after ECS application churn and restoration. Contact-index inputs now sort by stable entity ID before deriving aligned spatial boxes. Collision shapes and sensing rules are unchanged; exact continuation assertions remain in place.

## Review

Independent correctness and architecture review identified retained request handling, compartment event addresses, logical modifier owner resolution, officer application identity, stale quote comparisons, target rebinding, saved remainder/capture bounds, mixed-funding starvation and survivor caps, and fractional rate-source retirement. These were corrected with synthetic regression cases. Architecture feedback led to sharing compiled property definitions across compilation passes and removing an unused snapshot helper. Source retirement reuses existing native lifetimes rather than adding another registry.

The reviewer rechecked the corrections and the final AI observation, stable contact-index ordering and named-module include changes, reporting PASS with no remaining actionable findings. The final independent review inspected immutable implementation `ceddc3abdc53120c41955baa312f95ff912beb45` against `3f736a6` and the native/threaded evidence logs, confirming PASS. Reviews were read-only and did not repeat builds. The later consolidation review is recorded separately below.

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

## Review consolidation - 27 October 2026

The follow-up against `ad2d65a` implements all four architecture recommendations:

- Native and custom modifiers share grouped target/source aggregates and one eligibility-aware reader. Custom attributes use the existing additive arithmetic; flags/enums retain priority, newest-application and refresh precedence. Derived aggregates rebuild on mutation or restore; maintained eligibility remains live, including retirement-time before values.
- Content loading, verification and World preparation pass one immutable property catalogue through nested ability, effect, rule, native fitting and Controller compilation. Standalone helpers can still compile their own scope. Editable catalogues do not retain a potentially stale property cache.
- Damage and property-event producers share native binding traversal. Damage retains reached-shield selection; property changes retain their typed before/after captures and next-tick queue placement.
- Fractional-rate retirement calls a parameterless dirty marker with an accurate name. Deferred reconciliation and native lifetime ownership are unchanged.

A synthetic mixed-contribution fixture covers native sensor rows alongside custom attributes, flags and enums; independent and separately gated maintained sources; expiry and source retirement; exact before/after events; and warm-save/cold-restore continuation. Existing refresh and owner-dispatch checks remain intact. No schema or gameplay contract changes are intended, and no measured performance improvement is claimed.

The code is committed in `9f73801` (catalogue compilation scope) and `0e81e2b` (modifier/event consolidation and regression coverage). Independent review of immutable `0e81e2b` against `ad2d65a` reports PASS with no remaining actionable findings. Review corrections included previously omitted transitive catalogue forwarding through native and scenario helpers. An initial native compile exposed a declaration-order issue in the shared scratch alias; moving its declaration to the existing rule-state header fixed it.

Focused native compilation passed. Four selected CTest suites passed (abilities, rules, combat and content/pack verification); the custom-property executable was run separately and passed, including the new mixed-contribution fixture.

The complete `scripts/check.ps1 -Target native` gate passed all 45 CTest suites, 31 launcher cases, production-content/build checks, all 12 network-service cases and generation of all 22 continuation traces. The network fixtures used the same short temporary build-directory junction described above, removed after the run. No expensive compilation ran concurrently with the network checks. Twenty-one unchanged native continuation fixtures matched `ad2d65a` exactly. The custom-property trace includes the added mixed fixture, so it is checked against the new WebAssembly trace rather than compared to the shorter predecessor trace.

The full `scripts/check.ps1 -Target web -Threaded` gate passed, including the complete threaded build, production launch/content-refresh preflight, system suites and all 22 native/WASM continuation comparisons. The expanded custom-property fixture matched across platforms. Standard repository formatting and `git diff --check` pass; the final evidence edit also passes its Prettier check. No source changes followed the immutable review or final gates.

Consolidation logs are `build/consolidation-focused-build-2.log`, `build/consolidation-focused-tests.log`, `build/consolidation-custom-tests.log`, `build/consolidation-native-gate.log`, `build/consolidation-native-trace-comparison.log`, `build/consolidation-threaded-gate.log`, `build/consolidation-final-format.log` and `build/consolidation-evidence-format.log`. The original migration results above remain evidence for their original revision. Manual acceptance and performance-measurement limits remain as stated above.
