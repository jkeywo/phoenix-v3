# Ability migration evidence

26 October 2026. Implementation follows the [ability migration plan](ability-migration-plan.md), based on `5e1d087` in an isolated worktree to preserve concurrent flight changes. The [authoring reference](ability-content.md) describes the compiled schema. Implementation, independent reviews and automated integration are complete. Manual acceptance remains separate.

## Initial implementation

Officers and equipment grant shared abilities through static source declarations or temporary effect applications. Preparation, instant activation, sustained intent, exclusions and source-owned recovery use one execution model. Instant/prepared starts and cancellation use the existing Controller request identities, consumption and outcomes; sustained powers retain persistent settings. Optional planning channels remain preferences operated through ordinary Controller trees.

Effects use the existing prepared group/stock transaction. An instant start and activation commit as one required group; prepared startup is a separate completed step. Non-proportional operating demands that cannot be fully funded are removed and requoted before native allocation commits. Proportional powers preserve native allocation and exact psychic funding/fault rounding.

Native psychic contributions own physical projection shapes and derived work, linked to the shared execution. Observer identities, rejection/fade receipts, knowledge and native strongest-contribution rules remain separate. Officer recovery follows the officer through reassignment; it is no longer inherited from a choir seat.

World and portable session schema **26** preserve source grants/recovery, preparation, Controller request links, native projections and pending effects. Restore validates the compiled static roster, temporary effect references, actual source/control bindings, running-source uniqueness, exclusions, recovery reservations and generation tokens. Exact-expiry boundary state retires before the next physical work phase.

## Independent correctness review

The independent reviewer identified and prompted corrections for allied-target classification, simultaneous exclusion losers remaining queued, duplicate-source active grants on restore, exact-expiry saves, implicit recovery on preparation cancellation, missing discrete requests, stale grant input, removal identity collisions, sparse-state bounds, AI status availability, localisation and the maintained psychic smoke selector. Non-proportional funding was also moved before the allocation commit. The corrected snapshot passed static re-review; execution evidence is recorded separately.

## Focused validation

- Native `phoenix_abilities_tests`, `phoenix_psychic_tests` and `phoenix_system_tests` pass.
- Threaded WebAssembly `phoenix_abilities_tests.js`, `phoenix_psychic_tests.js` and `phoenix_input_check.js` pass under bundled Node **24.19.0**.
- Native/WebAssembly ability and psychic continuation traces match exactly. The synthetic journeys include source reassignment, preparation interruption, request replay/new requests, exclusions, temporary refresh/removal/expiry, late generation replacement, transaction rollback, native ghost continuation, partial funding, AI facts and malformed saves.
- The production base pack was generated and verified during implementation. Final full-content and shared-schema integration results are recorded below.

The new funding fixture uses integer-sized tick demands to distinguish actually partial work from legitimate fully funded pulses at very small per-second rates. Its native peer consumes from the same constrained store; rejected ability work must leave that peer's real consumption unchanged.

No graphical, physical screen-reader, touch/gamepad, six-display performance, Linux or browser-storage smoke acceptance is claimed for this migration. Earlier profiling results remain in [their own checkpoint](performance-followup.md).

## Architecture follow-up

The separate source audit found repeated catalogue/native adapter compilation, generated choir endpoint JSON roundtrips, Cartesian recovery validation, duplicate endpoint reset code and unused native economic fields. All five findings are implemented. Ability/native-profile definitions are compiled once into an immutable catalogue shared by fitted adapters; typed compilation reuses Personnel and control layouts. Choir endpoints use the same typed validator as generic endpoints. Restore validates only the actual recovery ledger entries against their precise source/ability or apparatus bounds. Dead psychic contact indexing, duplicate intent resets and unused native cost/recovery fields are removed. Ghost receipts retain ability identity separately from shared profile identity.

A fresh independent reviewer passed the architecture changes against initial implementation commit `4636957`. The refactored native ability, psychic and semantic suites pass, including real catalogue override/recompile and exact per-ability recovery-bound regressions. Final native/WebAssembly integration results are recorded below.

The broader architecture audit also inspected shared effect application, stock transactions, queues, property modifiers, native shield/damage integration, directed dispatch, payload delivery and periodic scheduling. It found no further actionable duplication: effects reuse the stock transaction used by processors/cargo/logistics/weapons, and shield/payload work shares the deferred queue. Specialist physical resolution and periodic cadence/cancellation retain their distinct responsibilities.

The first full native run passed 42 of 43 suites and exposed an optional-field mismatch in the Personnel projection: it added an empty ability list where the authoritative permitted roster omitted it. The projection now preserves omission while validating supplied lists; the existing Personnel integration case passes and the independent reviewer passed the correction. The correction is committed as `94c9233`. Concurrent flight commit `90d390d` was applied as `5fb5dbb` and its original ancestry merged as `c649910`, with no further source differences.

## Final integration record

The final native build, all **43** system integration cases, native build check and **20** continuation trace generators pass. `phoenix_verify` accepts the generated `base.zip` and `test-missions.zip`. The repository formatting check passes.

The first live-network run overlapped the full WebAssembly compilation and failed three process/timing cases; nine passed. It is not recorded as a passing gate. Running `scripts/check-network-service.ps1` after compilation completed passed all **12** cases, including every previously failing fleet/lobby case. The remaining native psychic and Controller trace generators were run explicitly after the interrupted wrapper; all twenty native traces are available for comparison.

The first complete threaded WebAssembly build succeeded, then its Node preflight stopped because the production Workshop worker was compiled for `worker` alone and its generated glue lacked Node's `Worker` adapter. Its supported environments now include `worker,node`, so Emscripten supplies its real `worker_threads` implementation. No fake worker, disabled thread pool, allocation change or exception-mode change was introduced. The independent reviewer passed this narrow correction. The full threaded WebAssembly gate passes after this correction and the fixture correction below.

The now-executing preflight exposed an older fixture expectation that edited bundled-base missions survived refresh. The existing native implementation and native menu tests deliberately replace bundled-base records while preserving independent user mods. The browser fixture now verifies that same contract, including an actual independent mod archive; no production refresh policy changed. Independent review passed the correction.

The complete `scripts/check.ps1 -Target web -Threaded` gate passes on final source revision `2fbeebd`, including production preflight, every configured WASM case and **20 exact native/WASM continuation comparisons**. This is Windows Release plus Emscripten **6.0.12**, threaded native WebAssembly exceptions/mimalloc and Node **24.19.0**. The ordinary browser deployment matrix and graphical/device/performance acceptance were not rerun.

Final evidence files in the isolated build directory are `ability-final-native-gate-2.log` (43/43 native suites, with the separately retried network failures retained), `ability-network-isolated.log` (12/12), `ability-final-web-gate-3.log` (passing complete threaded gate) and `ability-format-final-2.log`. No earlier failure is relabelled as a pass. The primary checkout retains the concurrent flight work; three earlier untracked ability files were preserved under `build/ability-preintegration-20261026/` before local integration.
