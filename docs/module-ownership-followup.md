# Module ownership after the AI migration

Implementation baseline: `fcb567f3` plus the working tree reviewed at the start. Demo/build, scenario-task and performance work continued independently during this refactor and remains separately owned. This structural follow-up preserves current scoring, physical admission, authored presentation, continuation and save/network formats.

The later [cold-start work](cold-start-2026-11-02.md) adds immutable verification receipts in content verification, permitted draft/companion validation in content distribution, and owner-driven acquisition in session-api. Local/remote/lobby composition shares those results. World keeps authoritative construction and recovery; its receipt overload reuses already compiled definitions, while editable catalogue inputs remain verified. Platform transport owns native/browser artifact IO, and foundation/data owns opt-in diagnostic emission. No mutable gameplay or operational knowledge moves into a cache.

## Ownership

| Owner                              | Responsibility                                                                                             |
| ---------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| Knowledge records definitions      | Full evidence stream identity, newest-first stream selection and provenance quality; no runtime dependency |
| Controller decisions               | Permitted observation preparation, source-specific candidate adapters and shared objective arbitration     |
| Controller objectives              | Independent bounded integer scoring/ranking target                                                         |
| Controller execution               | Action progression/cancellation, admission, report-receipt mutation and checkpoint encoding                |
| World command routing              | Private synchronous frozen/fallback capture, recipient restriction and retained inspections                |
| Tactical/flight/operations widgets | Existing specialised domain presentation; no Application/session retained                                  |
| Information widgets                | Message-history popup state, queues, timing, filtering and feedback requests                               |
| Console surface                    | Authored placement, DOM/cache lifetime, input routing and semantic publication                             |

Evidence selection marks the latest permitted stream entry before validity/payload checks. Stream identity retains scope, owner, schema, subject, source, publication and recipient. Consumers retain their privacy filtering, domain decoding, local-sample precedence and deterministic tie-breaking. No additional evidence cache or saved state is introduced.

Candidate adapters return typed frame records and own their scope, initial target/reasons and diagnostic evidence. Arbitration retains scoring passes/tiers, Captain marks, argument identity, commitments, backoff and bounded diagnostics. Existing authored candidate names and provider constraints are preserved.

Presentation extraction retains markup/classes, route/control IDs, Main/Details/compact placement, original source order and ordinary command dispatch. Message timing remains clock-driven with hover/focus pause and reconnect baselines; platform cue/announcement application stays in the host.

## Current ownership record

The earlier modularisation ledger remains a receipt for its migration cohort. These are the current follow-up moves:

| Previous owner                                                                           | Current owner                                                          |
| ---------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| `control/execution` observation/fact/candidate implementations and their context records | `control/decisions`                                                    |
| `control/execution` generic objective scorer                                             | Independent `control/decisions` scoring target; unchanged named module |
| Report identity/fingerprint helpers inside evidence candidate generation                 | `control/controllers/report_identity.cpp`                              |
| Successful-report mutation inside evidence candidate generation                          | `control/execution/report_receipts.cpp`                                |
| Repeated native Controller snapshot preparation                                          | Private `world/command-routing/decision_capture.cpp`                   |
| Specialised domain branches inside ConsoleSurface                                        | Existing tactical, flight and operations widget modules                |
| ConsoleSurface message-history state and rendering                                       | Existing information-widgets module                                    |

## Verification

- Baseline source ownership, dependency-refusal fixtures and Workshop shared-authoring architecture: pass.
- Baseline affected native integrations: 12/12 pass (Knowledge, personnel, Medical, logistics, docking, cargo, Away, craft, FTL, sensors, Controller runtime and input/semantics; 764.63 seconds).
- Portable Knowledge definitions, including the new evidence implementation: native build passes, zero warnings/errors.
- Final native Release build and build check: pass. The complete native suite passes **52/52** (`build/ownership-native-ctest.log`; 2444.35 seconds under concurrent work, not a performance benchmark).
- All 23 fresh native continuation journeys pass (`build/ownership-native-traces.log`). Controller and sensor continuations match the saved pre-refactor captures exactly. The sensor integration also checks that report diagnostics retain a positive source record identity.
- Native Courier Helm and Support journeys pass all three complexity profiles, normal/compact/enlarged text, specialised controls, Details, team-map dispatch and message-history opening/closing. The standalone message journey passes its narrow text, unread/reconnect, urgent announcement, pause, filtering, row width, scroll and return-focus checks.
- The existing independent shared-widget graphical fixture also passes readouts, automated/interactive Details, modal background-input rejection, ordinary command admission, compact reunion and Workshop ship/star previews (`build/ownership-shared-widget-details-smoke.log`). Its Details capture was inspected.
- Ordinary WASM full build and final dependency refresh pass (`build/ownership-ordinary-final-build.log`, `build/ownership-ordinary-recheck-build.log`). Six short affected integrations pass: input/semantics, Knowledge, personnel, flight, Workshop and preference bindings (`build/ownership-ordinary-short-gate.log`).
- Clean threaded WASM full build and final dependency refresh pass (`build/ownership-threaded-build.log`, `build/ownership-threaded-recheck-build.log`). The same six short affected integrations pass (`build/ownership-threaded-short-gate.log`).
- Eleven further affected continuation journeys pass in each WASM mode: combat/contact/armament/repair, Controller/coordination, sensors/inspections/reports, Medical, logistics, docking, cargo, Away, craft, FTL and missions/delivery. Each matches its fresh native continuation exactly: **22 comparisons pass** (`build/ownership-ordinary-continuation-gate.log`, `build/ownership-threaded-continuation-gate.log`, `build/ownership-continuation-comparison.log`). Each mode ran 17 affected runtime journeys. Captures remain disposable build artifacts, not permanent production goldens.
- Direct pre-extraction comparison passes: the seven affected UI folders from `fcb567f3` were rebuilt only in the disposable snapshot, against the same runtime, pack and Courier fixture. Both Helm/Support journeys and fresh captures pass before and after restoration. Representative Main, compact, Support/Veil and message captures were inspected for preserved layout/controls; real-time sweep/notification progress is not a pixel golden. Captures are under `build/ownership-before-smoke/` and `build/ownership-smoke/`; per-journey results are in `build/ownership-before-*-smoke-final.log` and `build/ownership-restored-*-smoke.log`.

Final execution gates use `build/ownership-validation-source`, whose **747 first-party C++ files match revision `97fba93a` exactly**. After restoring the UI experiment, all **1,080 code/build/test files** match that revision with zero differences (`build/ownership-restored-source-check.log`). The normalized source fingerprint is `031fe971045174d17e37f28682c84ee3cfeb6283c533733785bf7b870ef6a492`. This avoids mixing module interfaces or pack contents from concurrent work into a running build. Later scenario commits and optimisation edits, including edits to Controller facts, remain separately owned and preserved. The native tree is `build/ownership-native`; the WASM trees are `build/ownership-web-ordinary` and `build/ownership-web-threaded`. Native builds use the standard CMake/MSBuild rules. These results establish the refactor on that fixed source set; they do not certify the combined, changing working tree.

## Delivery commits

| Commit     | Change                                                                      |
| ---------- | --------------------------------------------------------------------------- |
| `0afe88f1` | Portable evidence identity, stream selection and quality                    |
| `92576f2e` | Decision/scoring ownership and synchronous World capture                    |
| `d8b6fe58` | Private candidate adapters alongside domain facts                           |
| `7e03d53c` | Specialised domain presentation in widget owners                            |
| `6a496b36` | Message-history panel lifecycle                                             |
| `97fba93a` | Interface/build corrections and meaningful diagnostic/graphical regressions |

Final execution receipts apply to `97fba93a`, including its corrections, rather than establishing that each intermediate tree passed the complete gate independently.

Concurrent scenario delivery uses World persistence schema 51. The optimisation task identified the corresponding stale schema-50 Controller assertion and its later integration updates that assertion to 51/retired 50. The execution snapshot above remains at schema 50 and was freshly rebuilt; its passing suite is not evidence for the later mixed schema-51 tree. This refactor does not change either save format or that separately owned migration.

Graphical evidence here is native. Actual browser rendering, physical screen readers, physical controllers/touch, six-display performance and public-network interoperability remain separate acceptance evidence.
