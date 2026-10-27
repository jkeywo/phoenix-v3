# Shared finite work — implementation and verification

Implemented on `main` in `e7b214ba995e7ed19d076e09936894df1457fc3b`, against base `20b202e`. The [approved plan](finite-work-migration-plan.md) and [authoring contract](finite-work-content.md) describe the ninth migration. The original migration checks below executed on that source revision; subsequent cleanup evidence names its own revision. This receipt does not establish manual acceptance.

## Delivered boundary

Finite effect-only jobs and native repair, treatment, stabilisation, facility recovery, docked energy/ammunition servicing and FTL preparation use shared process execution. Concurrent target episodes remain bounded entries in the existing process runtime, governed by their parent declaration or grant. Patient/fault progress, source-purchased structural work, bay throughput and physical delivery/FTL commitments retain native owners.

Native outcomes, work effects and input fees prepare and commit through the existing transaction. Completion consequences are once-per-run independent transactions; failure never rolls back completed native work. Static and temporary self-removal preserve committed receipts and cannot revive stopped state. Explicit restart retires old run credits, and true stops reset finite state consistently. No additional scheduler, wallet or ability runtime is introduced.

Typed structural/fault repair, treatment, stabilisation and recovery reuse native outcome helpers. Existing controls, scoped human/AI observations, Workshop editing/export, maintained content and strict World/portable-session schema 31 are included. Older sessions require a new game. Shared bay throughput rates use source-side facts; recipient-specific requirements and work costs use the concrete target. FTL readiness is reread after completion consequences rather than latched from an earlier reserve snapshot.

## Executed verification

| Check                                  | Result                                                                                                                                     | Evidence                                                            |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------- |
| Complete native gate                   | PASS: full build/content check, all 46 CTest suites, 31 launcher cases, all 12 live-network cases and 23 refreshed continuation references | `build/finite-native-gate.log`                                      |
| Independent development packs          | PASS: `test.zip` and `test-3d.zip` generated and verified                                                                                  | `build/finite-development-packs.log`                                |
| Complete threaded WebAssembly gate     | PASS: full threaded build, system suites, production launch/bundled refresh preflight and all 23 native/WASM continuation comparisons      | `build/finite-threaded-gate.log`                                    |
| Focused ordinary WebAssembly           | PASS: 14 suites, production launch/bundled refresh preflight and 12 exact native/ordinary continuation comparisons                         | `build/finite-ordinary-build.log`, `build/finite-ordinary-gate.log` |
| Predecessor native physical comparison | PASS: zero differences for Medical, craft and FTL                                                                                          | `build/finite-physical-comparison.log`                              |

Ordinary coverage includes content and Workshop, plus processes, Medical, craft, FTL, combat, away, logistics, processors, store effects, custom properties, abilities and rules. Its 12 continuation comparisons cover those named simulation suites. This is focused ordinary evidence, not a second full threaded matrix.

The new synthetic cases cover finite fractional completion, once-only and failed completion consequences, explicit restart, cancellation, shortage stop, source withdrawal and self-removal, concurrent patients and craft, target-owned progress, native structural prepayment, mixed electrical/quantity funding, thermal protection, contributor replacement, bounded accelerated final work, actual partial deliveries and preserved receiving claims, FTL ready upkeep/depletion and completion consequences, scoped projections and Workshop export. Existing assertions and tuning expectations remain intact.

Network timing checks ran after compilation completed. Both complete gate commands exited successfully; earlier focused iterations exposed implementation and fixture issues that were corrected before these final checks. No failing case or timeout was suppressed.

## Physical comparison and review

The same test-only capture code runs against the retained predecessor source and migrated source. Checkpoints retain motion, native resources/heat, patient/team state and revisions, material inventories and credits, transfer IDs/routes/claims/progress, craft/bay state and away work. Normalization excludes schema and top-level execution records and removes only the new servicing attribution from transfer records, alongside the previously documented neutral machinery metadata. No native quantities, work credits or revisions are normalized away.

The predecessor source audit compares 378 source/support files against `20b202e`. Apart from checkpoint instrumentation, it finds two pre-existing non-work differences: an equivalent default `std::nullopt` rendering argument and the empty-ability-controls observation guard. Neither changes the native work being compared. They are retained rather than overwritten. See `build/finite-predecessor-source-check.log`; before/after captures are in `build/finite-before/` and `build/finite-after/`.

Independent correctness and architecture reviewers both report PASS for the immutable range `20b202e..e7b214ba995e7ed19d076e09936894df1457fc3b`. Their findings drove fixes for concurrent target identity, paid native credit ownership, true-stop/restart consistency, native funding protection and policy, actual delivery, self-removal receipts, final fractional work and live readiness. Reviewers did not rerun the gates.

Standard repository formatting (`scripts/format.ps1 -Check`) and `git diff --check` pass; the complete formatting log is `build/finite-final-format-check.log`. These checks are separate from behavioral evidence. The Windows environment uses installed Node 24.13.0 through the scripts' expected SDK path and Emscripten 6.0.12. The main checkout's incomplete ignored SDK was preserved as `.tools/emsdk-before-finite`; `.tools/emsdk` reuses the complete local SDK through a junction. No tracked toolchain policy changed.

## Effect code cleanup - 27 October 2026

Source `bfe1deeef0bf4383c7808794c7eb2fc68de5c99a`, against `05d3ba3`, implements all four follow-up recommendations. The three credit-writeback paths share one canonical update helper, retaining their distinct funding and commit guards. Direct outcomes and finite native work share lazy scalar snapshot accessors that capture each original once and reuse prepared values. Driver selection evaluates real driver/target context without fabricated process IDs or generations; real admission retains lineage validation and rechecks eligibility after lifecycle effects. Completion has one mutable flag; schema-31 saves and observations retain `completion_delivered` as a derived alias, with contradictory aliases rejected on restore.

The independent reviewer reports PASS for that immutable source range. Focused process, Medical, FTL and rule suites pass, including actual-target selection, eligible-alternate and incumbent retention, once-only entry costs, restoration and invalid completion-alias cases. Existing concurrent-patient and atomic-outcome cases retain their assertions.

Final integration results on that source revision:

- Complete native gate: PASS, all 46 CTest suites, 31 launcher cases, 12 live-network cases and all 23 continuation references (`build/effect-cleanup-native-gate.log`). Compilation completed before the live-network cases.
- Exact before/after native continuation: all 23 traces match the pre-cleanup references, with zero differences and no state/value normalization (`build/effect-cleanup-native-comparison.log`; references in `build/effect-cleanup-before/`).
- Complete threaded WebAssembly gate: PASS, full build, system suites, production launch/bundled refresh preflight and all 23 native/WASM continuation comparisons (`build/effect-cleanup-threaded-gate.log`).
- Focused ordinary WebAssembly: PASS, 14 suites, production preflight and 12 native/ordinary continuation comparisons (`build/effect-cleanup-ordinary-build.log`, `build/effect-cleanup-ordinary-gate.log`).

Schema, authored content, tuning and supported observations are unchanged. These are behavioral and continuation checks, not measured performance claims or new graphical/device acceptance. Standard formatting and whitespace checks are recorded in `build/effect-cleanup-final-format-check.log`. No source changes followed the reviewed implementation commit; the delivery record was updated afterward. Nothing was pushed.

## Acceptance limits

No graphical journey, actual screen-reader use, physical-device, six-display performance or Linux execution is claimed. Full destination-based FTL travel and launch/docking/away workflow redesign remain outside this migration. Nothing was pushed.
