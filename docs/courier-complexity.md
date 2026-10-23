# Courier complexity

Agreed design, 23 October 2026. This is the implementation contract, not a completion claim.

## Ship and interface

All playable Courier mission slots use the rescue-based fit, displayed as **Courier**. Remove displacement, the reusable shuttle and boarding torpedoes. Away deployment and recovery use direct docking, with compatible authored target ports and existing boarding eligibility rules. Preserve race/navigation, scenario objectives and starting circumstances without mission-specific Courier equipment fits.

Use one, two or three stars in the player interface and a read-only star display on the server. Clicking star N selects level N. Filled and empty sprites show the accepted level; interactive backgrounds, hover and keyboard focus distinguish selection from read-only display. Keyboard, controller and screen-reader actions remain equivalent.

Courier defaults to one star. One selection covers Helm/Tactical; another covers Support/Comms/Navigator. Automatic operation while vacant or Away is separate from the selected level. Cruiser retains its current allocations, presented as one/two/three stars.

## Helm

| Level | Player                                                                                                                                                 | Assistance                                                                                                                                    |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------- |
| One   | Full assisted flight, shared combat target, individual offensive weapon enablement, docking and mission start/dispatch/recall/cancel.                  | Ammunition and other weapon settings, PDC interception, scanner following the combat target, mission preparation and Duty Officer assignment. |
| Two   | All one-star choices, direct flight and PDC enable/disable.                                                                                            | Enabled PDC chooses defensive or offensive firing; other assistance remains.                                                                  |
| Three | Full fitted flight/weapon/ammunition/PDC controls, individual weapon target overrides, mission asset assignments and manual Duty Officer reassignment. | Ordinary execution; scanner still follows the shared combat target.                                                                           |

Shared target changes preserve explicit three-star weapon overrides. Downgrading removes those overrides and returns enabled offensive weapons to the shared target. Downgrading from direct flight releases held inputs and resumes assistance. Scanner control does not become a separate task at higher levels.

## Support

| Level | Player                                                                                                                                                          | Assistance                                                                                                                              |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| One   | Repair priorities; conversations, hails and away dialogue; authorise engineering/Medical contributions; FTL preparation, cancellation and deliberate departure. | Reactor/batteries, shields/redistribution, repair-team allocation, all Medical work, radar/radio readiness and Duty Officer assignment. |
| Two   | All one-star choices, direct reactor output, individual shield raise/lower and balanced/forward/aft protection priority.                                        | Batteries, shield redistribution, repair-team allocation, Medical and Duty Officer assignment.                                          |
| Three | Full fitted power/shield controls, individual repair teams, Medical orders/patient priorities/recovery and manual Duty Officer assignment.                      | Ordinary machinery and team execution; radar/radio readiness.                                                                           |

Comms has no separate radio enable/disable control. FTL retains the same simple preparation/cancel/departure interface at every level. Its supply and cooling belong to power management. AI neither overrides two-star reactor output nor initiates departure. Show readiness, docking blockers and personnel/craft outside the ship.

Below three stars, officer assignments for duties and authorised away contributions are automatic and department-scoped. Teams and officers are distinct: contributing a team still requires the owning Station's authorisation. Three stars allows manual officer reassignment without permitting another Station to take its personnel.

## Delivery boundaries

Implementation must reuse authored Controller profiles, ordinary trees and admitted commands. Hidden controls must also be refused authoritatively. Station-level profile changes are atomic and retain accepted physical work. AI uses permitted observations, finite resources, real travel and existing docking/mission rules.

The initial changes add star metadata, star presentation and opt-in station-level profile propagation. They do **not** yet establish the standard Courier fit or its agreed automation.

Still required before this increment is complete:

- Convert Courier mission selections, navigation dependencies, fit, docking and mission-target ports.
- Author the three workload profiles and missing execution policies.
- Enforce direct-flight, weapon, PDC, shield, Medical and personnel permissions.
- Verify grouped profile handover/recovery and all workloads through independent synthetic fixtures.
- Complete native/WASM validation and explicitly invoked graphical journeys; record physical-device and reader acceptance separately.

During implementation, an independent shield-effects refactor is modifying the shared content and simulation sources. Integrate against its finished definitions rather than treating intermediate build failures as validation of this increment.
