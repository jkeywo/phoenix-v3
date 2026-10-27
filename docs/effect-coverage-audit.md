# Effect overhaul: final coverage audit

27 October 2026. Audit baseline: `f4d7fc740f7ddfbedea12d6bba87a1b6471f5d18` on `main`. This reconciles the [target design](gameplay-engine-design.md) with the nine [migration stages](gameplay-engine-migration.md). It covers existing gameplay conversion, not every future GDD feature or possible native operation.

## Coverage reconciliation

| Area                                        | Delivered shared representation                                                                                                                     | Native behavior retained                                                       |
| ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Duty Officers                               | Maintained rules, property contributions, grants and shared abilities; `personnel.cpp`, `personnel.hpp`, `abilities.hpp`.                           | Assignment, qualification, roster and travel.                                  |
| Psychic operations                          | Shared ability preparation, exclusions, costs, recovery, temporary grants and process execution; [ability evidence](ability-migration-evidence.md). | Psychic apparatus, findings, visibility and physical mechanisms.               |
| Weapons and ordnance                        | Directed modes, captured resource payloads, immediate/periodic effects and shared reactions; [directed evidence](directed-systems-evidence.md).     | Aiming, projectiles, collision and penetration.                                |
| Turrets and directed scanners               | Shared targeting/operating selection and properties; `sensors.hpp`, `directed.hpp`.                                                                 | Mount movement, acquisition and equipment-local evidence.                      |
| Shields                                     | Independent contributions, shared funding/properties and reached-layer damage hooks; `effect_damage.hpp`, `damage_rules.hpp`.                       | Shield reserves, geometry, layering, absorption and standard GUI.              |
| Resources, stores and recipes               | Canonical stocks, acceptance/capacity, costs, transfers and processors; [resource evidence](resource-store-evidence.md).                            | Physical supply connectivity, loading and delivery commitments.                |
| Generation, propulsion and cooling          | Shared continuous processes, lifecycle/work effects, costs and temporary grants; [machinery evidence](machinery-process-migration-evidence.md).     | Physics, allocation, exact thermal and reserve behavior.                       |
| Faults, repair and Medical                  | Shared finite work and typed structural/fault repair and clinical outcomes; [finite evidence](finite-work-migration-evidence.md).                   | Diagnosis, access, triage, patient/fault progress and native fault generation. |
| Teams, boarding and away                    | Shared requirements/properties and finite outcomes, plus direct tree effect groups added by this closure.                                           | Transport, encounters, orders, personnel and mission orchestration.            |
| Craft and FTL                               | Shared preparation, funding and completion effects, actual servicing receipts; `craft.hpp`, `ftl.hpp`.                                              | Movement, launch/recovery, docking, live readiness and deliberate departure.   |
| Communications and sensing                  | Shared radio/sensor properties and resource funding; directed scan execution; `conversations.hpp`, `sensors.hpp`.                                   | Signal delivery, dialogue, observation provenance and publication scope.       |
| Regions and fields                          | Shared entry/exit/occupancy rules, maintained contributions and hazards; `region_rules.hpp`; [rules evidence](rules-migration-evidence.md).         | Geometry, membership, native nebula and asteroid interactions.                 |
| Objectives and scenarios                    | Direct effect groups and reusable rules; `gameplay_scenario.cpp`, `scenario.cpp`, `store_effects.hpp`.                                              | Progression, outcome evidence and authoritative reporting.                     |
| Future facilities, conversion and campaigns | Existing recipes/processes/effect interfaces available for future content.                                                                          | New domain gameplay remains future work, outside this overhaul.                |

The audit found one implementation gap: native team and away trees could not directly invoke shared effect groups. Their native action dispatch was not evidence of that promised capability. This closure supplies it. No additional runtime conversion is justified by the retained native behavior in the table.

## Direct native-tree effects

Team and away trees support a once-per-activation action:

```json
{ "action": "apply_effect_group", "arguments": ["mod.effect-group", "target"] }
```

Both arguments are literal. The second is `self` or `target`. `self` selects the home ship. For away mission trees, `target` selects the mission destination entity. For team orders, it selects the current native work ship and compartment, including away field care and boarding-party care. It does not select an arbitrary nearby entity or automatically select a particular patient. Explicit outcome operations retain their authored patient/fault/compartment fields.

The frozen context identifies the home source separately from the selected subject/target. Team source and owner include actual team equipment; costs can charge that equipment. Effects do not overwrite the native team activity. Authored tree conditions decide when to invoke an effect; direct operations do not implicitly enforce team arrival, diagnosis or facility requirements. Native repair and treatment jobs continue enforcing their own requirements.

Invocation uses the existing prepared effect transaction. A failed group changes no required outcome or payment and records the existing bounded failure receipt. There is no automatic retry. The saved once-task continuation prevents repeated running effects or replay after restore; away missions also retain their existing bounded action journal. A new activation may invoke the action again. Stable effect provenance includes the tree output activation/task/reference-call identity.

Production validation compiles literal effect references recursively through tree references and rejects invalid target roles. Existing generic dependency closure follows the resolved literal group and transitive property/resource references; inheritance is flattened before runtime export; no second export walker is introduced. Workshop trees remain ordinary authored JSON and production verification is authoritative.

## Scope qualifications

Physical operator trees were removed by the Controller refactor. Native machinery now applies work/lifecycle effects through shared processes; authoritative team, away and scenario trees have direct effect actions. Human and AI Controllers retain the same admitted commands and observations. This closure does not grant Controllers arbitrary authoritative effects.

Native damage and boarding can generate faults. `fault_repair` is a supported typed outcome; general authored fault creation is not currently a typed effect operation. Likewise, the supported shield-property contract does not promise dynamic modification of every geometric/accounting field. These are supported-operation limits, not missing migrations of existing gameplay.

Launch/recovery sequencing, docking, away orchestration and full destination-based FTL travel remain outside this conversion. Older stage documents describe their own historical delivery limits; subsequent stages supersede those limits where noted in the migration index.

## Persistence and acceptance

World and portable-session schema is 32. Appending the tree action vocabulary changes continuation signatures, so schema 31 and older sessions are rejected with an explicit new-session diagnostic. No parallel compatibility runtime or duplicate mutable effect journal is added.

Graphical journeys, actual screen-reader use, physical touch/gamepad devices, six-display performance and Linux execution remain separate acceptance work. This audit does not establish them.

## Executed verification

Implementation: `fd9bfab15aeddee8817ac7aba5a381964b4efac5`, compared with audit base `f4d7fc740f7ddfbedea12d6bba87a1b6471f5d18`. Independent read-only correctness and architecture review of that immutable range reports PASS with no remaining actionable findings. No source changes followed that review.

| Check                              | Result                                                                                                                                                                                                    | Local evidence                                                                              |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| Focused native                     | PASS: Medical and away suites, including source-equipment funding, native activity retention, selected compartments, referenced trees, resolved export/reload, atomic failure and no replay after restore | `build/effect-final-audit-focused-tests.log`                                                |
| Complete native gate               | PASS: 46 CTest suites, 31 launcher cases, 12 live-network cases, content/build check and 23 continuation references                                                                                       | `build/effect-final-audit-native-gate.log`                                                  |
| Complete threaded WebAssembly gate | PASS: build, system suites, production preflight and all 23 exact native/WASM continuation comparisons                                                                                                    | `build/effect-final-audit-threaded-gate.log`                                                |
| Focused ordinary WebAssembly       | PASS: 14 suites, production launch/bundled-refresh preflight and 12 exact native/ordinary continuation comparisons                                                                                        | `build/effect-final-audit-ordinary-build.log`, `build/effect-final-audit-ordinary-gate.log` |
| Inspectable predecessor state      | PASS for 12 fully inspectable captures: zero differences after 26 schema/signature-only changes                                                                                                           | `build/effect-final-audit-native-comparison.log`                                            |

The predecessor comparison preserved all 23 earlier trace files before the gate. Twelve are fully inspectable. Eleven contain changed opaque digest lines; those digests are explicitly excluded from predecessor-equivalence claims, rather than accepted as normalized physical state. Inspectable JSON permits only World/session schema 31 to 32 and compiled continuation signature changes. Native quantities, progress, personnel and other gameplay state are not normalized. Current native/WASM comparisons use all 23 complete traces without normalization.

Both complete gate commands and the ordinary runner exited successfully. Live-network timing checks ran after native compilation and before the WebAssembly builds. Earlier focused iterations caught a compile-time catalogue variable error and two export-fixture assumptions; these were corrected without disabling any check. The resolved export test uses the production ZIP reader/resolver without rerunning fixture-only shorthand generation.

Standard repository formatting and whitespace checks pass; `build/effect-final-audit-final-format-check.log` records the formatting check. No graphical/device/Linux acceptance is inferred. Nothing was pushed.

## Obsolete implementation cleanup

Following the coverage audit, the cleanup removes three unused helpers (`officer_property_delta`, `property_available` and `reprepare_effect_group`) and nine unreferenced development-pack process definitions: `test.process.processor`, `test.process.recovery`, and the `test3d.process` processor, repair, Medical, recovery, servicing-energy, servicing-ammunition and FTL templates. Maintained base-pack definitions and active native mechanisms remain.

The away reader now accepts only the current per-mission schema 11. World/session schema remains 32: current records and continuation behavior are unchanged. The outer schema already rejects genuine historical sessions, so inner schema 1–10 reconstruction and clinical-tree restarts no longer serve a supported import path. Synthetic tests now reject those inner formats even when wrapped in a current World envelope; current travel, partial treatment and exact restore checks remain. Named scenario shield/modifier actions retain their distinct grant-refresh/removal semantics.

Cleanup verification is recorded after execution; earlier evidence above applies to its named revision.
