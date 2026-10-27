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

Automated validation and independent review evidence will be recorded here after execution. Graphical journeys, actual screen-reader use, physical touch/gamepad devices, six-display performance and Linux execution remain separate acceptance work. This audit does not establish them.
