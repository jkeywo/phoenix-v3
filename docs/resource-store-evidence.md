# Resource and store migration evidence

23–24 October 2026. Implementation and automatic validation are complete on `codex/resource-store-unification`, based on `f4aec12`, in an isolated worktree to preserve concurrent changes in the primary checkout. This record accompanies the verified migration commit and follows the [migration plan](resource-store-migration-plan.md).

## Ownership and execution

`StockStore` owns actual quantities, selected resource, receiving-space reservations and stock claims. Physical stores live in `ShipStockStores`; magazine and logistics entries reference the same store. Electrical and coolant states own the same store type alongside their native heat, flow and fractional operating state. There are no parallel magazine, energy or coolant quantity counters.

Resource and recipe definitions participate in pack inheritance, explicit overrides, dependency closure and Workshop authoring. Capacity uses a resource's size under the store's selected tag. [The identity migration](resource-identity-migration.md) records maintained-content identities and batch sizes.

Weapons capture loaded resource and quantity and reserve their return space. Dispatch removes actual stock and reserves the receiving store. Cargo jobs and processors retain their own commitments. A single-resource store keeps its selected resource when empty; an admitted change requires no contents or commitments and cancels obsolete waiting requests.

Processors use exact recipe inputs and outputs, batch or continuous work, native resource funding and property modifiers. Required outputs must fit; completed output is never clipped. Recipe changes discard unfinished work without refunding consumed ingredients. Processor progress and commitments survive interruption and replacement restore. Continuous recipes have an explicit supported ratio period of at most 4096, checked by the compiler and runtime constructor; see [authoring rules](content-format.md#resources-stores-and-recipes). Batch recipes are unaffected. The shared-store solver enumerates bounded periodic residues and solves the remaining linear intervals exactly; it never abandons a search after an arbitrary iteration count.

Trusted `apply_effect_group` actions and shield reactions share preparation for store, shield and modifier operations. Entry energy is paid from pre-operation stock. Failed preparation publishes no stock or contribution changes. Execution receipts prevent a resumed action from applying twice. Resource-attached effects remain inert until a consuming mechanism invokes them.

## Continuation and review

The initial migration used World schema 22 and electrical-network version 2; the architecture follow-up below advances these to 23 and 3. Restore binds stock records to compiled definitions and checks physical deliveries, loaded weapons and processors against their exact commitments. Unknown or orphan claims fail replacement restore.

The independent source review found and prompted corrections for loaded-round reservation validation, orphan weapon reservations, processor work lost at batch boundaries, feasible partial conversion within a shared store, pathological rounded-ratio search and small property modifiers lost before fixed-point scaling. Source re-review passed after the bounded periodic solver, milliwork modifier evaluation and generic team-processor targeting fixes. The reviewer also checked the final session-schema and portable selector changes; there are no outstanding findings. Runtime gates are recorded separately below.

## Validation record

- Native shared definitions and content compilation passed.
- The first runtime compile exposed a missing nonthrowing stock swap guarantee; explicit member swap replaced it.
- The first integrated simulation compile exposed remaining retired magazine accesses and a missing processor selection revision; those integration points were replaced.
- Initial runtime attempts exposed assembly validation placeholders without resource identities and JSON temporary-lifetime errors in processor/logistics compilation. The integrated gameplay case passed after those fixes.
- Processor integration passes with the final bounded-search implementation, shared-store rounding, prepaid inputs, small signed modifiers, long resource identities and their regression cases.
- Pack verification found a retired ammunition check, mismatched inherited magazine aliases and missing 3D-pack resource bindings. These were fixed before the final complete corpus verification.
- Focused runtime checks passed shared store effects and processor execution. Other cases exposed a missing ended-duel reload guard, a free-processor compiler path assuming an electrical network, mismatched synthetic ammunition identities, stale negative-test diagnostic assertions and an overbroad craft-service test observation. These were corrected before the full gate; resource/recipe identity bounds and projected resource names were aligned as well.
- The first full native build passed. Its CTest run passed 30 of 39 cases and exposed three remaining integration causes: a flight-fixture conversion dropping its resource definition, old inventory writes in cargo/mission fixtures, and the local-session import guard still requiring schema 21. The fixtures now use explicit resources/shared stocks and the session guard requires schema 22. A complete native rerun validated these fixes.
- The second native run passed all 39 CTest cases and the native application build check. All 17 native continuation traces were recorded successfully; the two traces after the network step were completed separately after that step's initial failure.
- Production packaging verifies base, test-missions, test and test-3d with the migrated schema.
- WebAssembly compilation exposed a recipe-selector comparison between a string and JSON value accepted by MSVC. Explicit string extraction fixed the portable C++ type boundary; the complete WebAssembly build and corresponding native application rebuild then passed.
- The live-network gate found a missing explicit energy resource in its separate playable-fleet fixture; that fixture was migrated and its probe rebuilt. Lobby journal lock filenames reached Windows' 260-character path limit in the managed worktree. The complete `scripts/check-network-service.ps1` rerun passed all 12 cases with `PHOENIX_NATIVE_BUILD_DIR=C:/Coding/Phoenix V3/build/rs-net-24`, a short writable staging directory supported by the harness. An initial fleet cleanup `EPERM` did not recur. No journal exclusivity or network assertions were weakened.
- The final native application rebuild and `native.exe --build-check` passed after the portable selector correction. `scripts/check.ps1 -Target web` passed its complete build, content preflight, integration cases and all 17 native/WASM continuation comparisons, including processors and store-effect groups.
- Repository formatter checks passed for all migration-owned files; `git diff --check` passed. The existing formatter virtual-environment launchers referenced an unavailable Python path, so an ignored local shim invoked the same installed formatter packages directly. Unrelated pre-existing formatting was left outside this migration.

Reproduction: use `scripts/check.ps1 -Target native` with a short writable `PHOENIX_NATIVE_BUILD_DIR` when running from a deep Windows worktree, then `scripts/check.ps1 -Target web`. Verify development content with the `development_packs` native target. Builds used separate native and WebAssembly output trees; reused pinned dependency sources were not used as evidence of this migration's test results.

No graphical, touch, gamepad, screen-reader, six-display performance or browser storage smoke journey was run for this migration. Those acceptance areas remain separate from the automatic integration evidence. Directed systems, weapon payload effects and new fabrication/recycling gameplay remain deferred.

## Architecture consolidation - 24 October 2026

The follow-up to `28faf12` removes the five duplication/copying issues found in source review:

- Authored resources and stores have one JSON compiler in `phoenix.content`. The runtime shares typed filter/definition validation; accounting fixtures construct typed definitions instead of maintaining a second authoring parser. Production compilation rejects contradictory `mode` and `require_all` filter settings.
- `StockStore` shares immutable definition bindings and a frozen resource catalogue. World physical stores and native networks share the same catalogue. Transaction snapshots copy mutable stock state and compare binding identity, without copying or deeply comparing the catalogue. Same-revision restoration and definition replacement still invalidate stale prepared changes.
- Processors use `StockTransaction` for preparation, output publication and recipe switching. Their separate candidate-store transaction implementation is removed. Batch/continuous rounding, funding and exact output reservations remain unchanged.
- Effects and processors use one named-store resolver, including canonical names, definition IDs, logistics aliases and ambiguity checks. Ordinary effects stage only touched stores. Native entry payment additionally prepares the payer's network because payment changes heat, discharge limits and telemetry as well as stock.
- Scenario shield, modifier and operation-group requests share one pending queue, operation vocabulary, confirmation path and restore validation. The existing authored actions, shield/modifier/group phase order, source identities and the existing saved request-array representation are retained. The three saved request arrays are compatibility serialization of the one runtime queue.

Regression coverage exercises production filter parsing, shared immutable bindings with independent mutable stocks, aliases in paid mixed stock operations and rollback of native heat/payment, mixed effect-family continuation, and invalid pending receipt replacement. The initial focused native run passed four of five cases; the new paid-effect continuation case exposed the missing network allowance described below. That case passed after the fix, including invalid-allowance rejection. Independent source review passed both the consolidation and the subsequent allowance fix. Integrated validation is recorded below.

The new paid-effect continuation regression exposed an existing gap: the resource network restored remaining charge/discharge allowances as zero, so a pending scenario effect could fail payment only after resume. Both allowances are now saved and validated. This requires network version 3 and World/session schema 23; prior versions are explicitly rejected. Phase ordering and native payment rules remain unchanged.

Follow-up validation:

- The complete native build, all 39 CTest integration cases and the native application build check passed. All 17 native continuation traces were recorded; the final two were run separately after the network step stopped the combined script.
- Base, test-missions, test and test-3d packs verified successfully, including the `development_packs` target.
- The first network run passed ten of twelve cases. Two lobby admission cases timed out while the WebAssembly optimizer was also running. The complete separate network rerun passed all 12 cases after WebAssembly compilation finished, with unchanged assertions and time limits. No network code was changed for these timeouts.
- The complete WebAssembly build and `scripts/check.ps1 -Target web` passed: content preflight, all integration cases and all 17 native/WebAssembly continuation comparisons, including processors and paid stock effects.
- The final repository formatting check and `git diff --check` passed for the follow-up changes. Validation logs are retained under `build/effect-cleanup-*.log` in the implementation worktree. No manual graphical smoke journey was run.
