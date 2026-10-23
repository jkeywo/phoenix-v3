# Cruiser Science, Comms and Captain

Implementation contract, 23 October 2026. These workloads complete the [Cruiser station revision](cruiser-complexity.md). Stars select player responsibility; vacant/Away consoles independently use their ordinary AI policies.

## Science

| Level      | Player                                                                                                                                                                                                                                               | Assistance                                                                                                                                                                                                                            |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| One        | Select the Science contact; raise/lower individual shields and choose balanced/fore/aft/port/starboard protection; assign individual Medical teams to compartments; authorise Medical/displacement mission contributions; prepare/cancel/depart FTL. | Both 360-degree scanners follow Science selection. AI redistributes shields, chooses care within assigned compartments, manages readiness/local stocks and assigns officers. Ward protects the Cruiser; Phantom choir stays inactive. |
| Two adds   | Independent scanner targets; clinical orders and patient priorities; enable/disable and target Phantom choir and Psychic ward.                                                                                                                       | Shield redistribution, readiness, local stocks and officers remain assisted.                                                                                                                                                          |
| Three adds | Shield discharge, fitted equipment settings, local medical stocks and manual Science/Medical/Navigator/psychic officer reassignment.                                                                                                                 | Physical execution continues normally.                                                                                                                                                                                                |

Science has no Dispatch or shuttles. Tactical and Engineering retain mission ownership; Science authorises its own contributions. Engineering owns supply/cooling, including FTL and psychic machinery. FTL departure remains deliberate at every level.

Assigning a Medical team retains its compartment through treatment and infirmary recovery trips. It returns there after recovery and waits if no eligible patient needs care. Specific clinical orders take precedence; a new compartment assignment clears the patient priority. Existing mission reservations and physical travel continue to apply.

Scanner targets are independent of Tactical targeting. Individual overrides persist at two/three stars; downgrading returns both scanners to the shared Science target. Hidden individual machinery settings return to their authored initial settings when assistance resumes.

## Comms and psychic fittings

Comms has a fixed one-star workload: hails, conversations, negotiations, requests, opportunity discovery and consequential away-party dialogue. Radio readiness, its reserve and Signals officers are automatic. There is no radio toggle or psychic control. Accepted station supply offers reveal Engineering missions through the existing Dispatch mechanism without launching assets.

Phantom choir and Psychic ward, their specialists and apparatus controls move to Science. Cruiser loses Veil and its specialist seat. The standard Courier gains Veil under Support, with an actual local reserve, supply and cooling: automatic operation at one star, player enable/disable at two, specialist reassignment and machinery settings at three. Existing emissions, resources, officer availability and physical psychic rules determine effectiveness.

## Captain

Captain has a fixed one-star workload: alerts, supported stances for wholly AI-operated consoles, and the new Objectives list. Its former reserve, service assembly, Engineering service controls and Command personnel roster are removed. Captain has no shared-report dashboard.

The list displays only objectives published shipwide, including known status, description and destination. Any effectively AI-operated Controller permits selection of one active priority, including a delegated Controller within a staffed station. Stance authority remains restricted to wholly AI-operated target consoles.

Priority is advisory. Autonomous objective-following navigation prefers its published destination; explicit flight orders, Hold position, human control and physical commitments retain precedence. Other AI can read the priority but this increment does not add combat, scanning, logistics or mission-planning responses. A condition objective without a destination does not create a navigation order.

Completion, failure, cancellation or withdrawal clears the priority without choosing a replacement. Save/resume and handover retain valid selections. With no AI, the list remains readable and the selection is retained but cannot be edited or influence human controls.

## Authoring and continuation

- Console `objectives: true` enables the list and admitted `objective.priority` select/clear action. The ship blackboard and permitted Console projection expose `objective_board` with the published list, selected identity, revision and current selection eligibility.
- `control_layout.fixed_profiles` maps Console IDs to their only permitted profile, also supplying their default. Lobby, initial setup, live commands and restoration enforce it.
- `manual_workloads.medical` sets the minimum stars for specific clinical orders. The existing `team.order` path accepts `auto/<compartment>` for compartment assignment; patient/order controls are withheld below the threshold.
- Controller observation `own.id` supplies a numeric self target for protective policies; `objective.priority` exposes the selected identity. Ship-objective record bindings may use `subject: "priority_or_latest"` for navigation, falling back to ordinary latest-publication behaviour when no applicable destination is selected.
- World save schema is **21**; older saves require a new session. Priority state and assigned Medical compartments survive continuation.

Verification uses synthetic Controller/objective, Medical and psychic fixtures plus actual content verification. Native/WebAssembly evidence and graphical/device limitations are recorded separately from this contract.

## Verification evidence — 23 October 2026

The native build and `native.exe --build-check` pass. Production base and test-mission packs verify. Synthetic Controller runtime, Medical, psychic, sensors, personnel, away, FTL, motion/radar and local-session checks pass. The new cases cover priority admission/publication scope, replacement and clearing, stale commands, partial AI, continuation, terminal objectives, persistent Medical compartment assignments and protective psychic assistance across workload changes.

WebAssembly server, client and Workshop builds pass; Controller runtime, Medical and psychic integration executables pass under Node. Save/resume equivalence is asserted within the synthetic fixtures on each platform. Whole-state trace hashes from these runs are not an exact cross-platform comparison: concurrent ownership/interdiction work changed the shared runtime between builds. Those unrelated changes are excluded from this station commit. Formatting and staged whitespace checks pass.

The explicitly invoked `phoenix_cruiser_ui_smoke base.zip test-missions.zip --remaining-stations` run passes on Windows at 1920×1080. Its free-flight variant publishes a demonstration objective and captures Science at each workload, Comms and Captain. Images are under `build/station-work/`; Captain's selected objective and clear control are visible at the top of the console. This is graphical evidence, not a production-content balance assertion.

The new Objectives readouts and controls use the existing semantic/localisation path. A physical screen-reader, touch/gamepad and six-display acceptance run has not been performed for this change. The private signalling-service lobby journey was not run; fixed-profile admission is exercised through the synthetic World fixture.
