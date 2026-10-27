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

Helm has no separate Docking tab. Select a contact on the radar: its info panel offers **Dock** when an owned port can dock with it, **Cancel docking** during approach, and **Undock** when connected. Separation shows status until finished. A normal Dock uses the existing assisted approach when flight guidance channels are available. Port permissions, supply-service settings and emergency release remain specialist docking controls rather than routine Helm buttons.

## Support

| Level | Player                                                                                                                                     | Assistance                                                                                                                                                       |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| One   | Repair priorities; conversations, hails and away dialogue; FTL preparation, cancellation and deliberate departure.                         | Reactor/batteries, shields/redistribution, repair-team allocation, all Medical work, away team contributions, radar/radio readiness and Duty Officer assignment. |
| Two   | All one-star choices, direct reactor output, individual shield raise/lower and balanced/forward/aft protection priority.                   | Batteries, shield redistribution, repair-team allocation, Medical and Duty Officer assignment.                                                                   |
| Three | Full fitted power/shield controls, individual repair teams, Medical orders/patient priorities/recovery and manual Duty Officer assignment. | Ordinary machinery and team execution; radar/radio readiness.                                                                                                    |

Comms has no separate radio enable/disable control. FTL retains the same simple preparation/cancel/departure interface at every level. Its supply and cooling belong to power management. AI neither overrides two-star reactor output nor initiates departure. Show readiness, docking blockers and personnel/craft outside the ship.

Below three stars, officer assignments for duties and authorised away contributions are automatic and department-scoped. Teams and officers are distinct. AI-operated team Controllers authorise and prepare their department's contribution when a mission is activated, including on an occupied Console. Human-operated team Controllers retain department authorisation. Three stars allows manual officer reassignment without permitting another Station to take its personnel.

Personnel is shown only when the station can manually assign officers (currently three stars). One- and two-star automatic assignments do not get a Personnel tab. Away Missions belongs to Helm's Dispatch. Support has no mission lifecycle tab: once Helm opens a mission, eligible team details offer sending that team on it, and manual officer rosters offer the mission against eligible officers. These choices use the existing Station-owned contribution commands. At lower stars, team contributions retain automatic officer selection. Closing/cancelling a mission removes its assignment choices. At one star, a direct-docking mission offers **Activate mission**: AI prepares eligible teams and their officers, and the first insertion proceeds through ordinary dispatch admission once the party is staged and Helm has physically docked at a compatible port. No second Send action is needed. Activation does not launch a shuttle, recover a party or redeploy a recovered party automatically.

### Support presentation

Support uses three peer views: Power, Repair and Comms. Power is one scrolling panel: reactor output, deduplicated battery and coolant reserves at the top, followed by power-group rows at lower complexity and individual reserve/System rows at three stars. Each row shows its name, permitted power slider, coolant slider when the profile grants control, and battery reading. Shared allocations are identified rather than duplicated. AI-operated machinery is labelled Automatic; unavailable coolant sliders are omitted. Remaining shield, FTL and Support controls stay on this panel, with Navigation available as a details view.

Repair is one panel with the section/damage list alongside the repair-team roster and each team's current activity/location. Assisted repair priority is selected directly on the panel. Clicking a team opens its details and the deployment/priority controls permitted at that star level. Teams have no player-operated readiness switch: they are idle, travelling, working, assigned away or unavailable through condition/loss. Automation chooses work rather than making a team ready. Medical and other support-team details remain accessible from Power. Individual System fault details are projected only while a functioning, available repair team is physically in that section; travelling, lost, disabled or away-reserved teams do not provide that observation. Section-level damage remains visible without an inspecting team. Operational telemetry still reports actual machinery performance. Comms retains its dedicated conversation workspace. These views preserve the authored command allocations and Console ownership.

## Delivery boundaries

Implementation must reuse authored Controller profiles, ordinary trees and admitted commands. Hidden controls must also be refused authoritatively. Station-level profile changes are atomic and retain accepted physical work. AI uses permitted observations, finite resources, real travel and existing docking/mission rules.

The implementation uses the common `base.rescue-courier` definition for playable Courier selections. The unused `base.delivery-courier` compatibility alias has been removed. Mission-specific objectives and initial cargo remain authored scenario data. NPC patrol definitions are unchanged.

The three profiles divide existing Controller command channels and reuse ordinary state trees. New runtime support covers shared combat targeting, split direct-flight bindings, department-scoped officer assistance, authorised team contributions while Medical is automatic, and conversation authority independent of radio readiness. Automatic Medical selects eligible authored patient orders and clears completed work when starting another order. FTL departure remains a deliberate player action.

Host command admission enforces the same permissions as the interface. An opened target-authored boarding mission can permit direct docking only at a compatible boarding port once its existing insertion condition is met. Officer and team travel, capacity, injury, resource costs and recovery continue through their existing physical paths.

Validation uses independent synthetic Controller, personnel, Medical, conversation, away and boarding fixtures, plus verification of the actual base and test-mission packs. Graphical smoke checks are explicitly invoked evidence; physical touch/gamepad and screen-reader acceptance remain separate platform checks.

Native verification on 23 October 2026 passed the application build check and the Controller runtime, personnel, Medical, conversation, away, security, content and fleet-setup cases. The explicit lobby smoke passed at its authored landscape/portrait sizes and with a separate local console region. Its captures show the standard Courier's one-star defaults and distinct read-only/server versus clickable/console star backgrounds. Builds used `build/courier-native` and `build/courier-web` to avoid sharing generated outputs with concurrent work.

The browser server, client and Workshop builds also passed. WebAssembly passed the Controller runtime, personnel, Medical, conversation, away and security cases; Controller, Medical and security continuation traces matched native exactly. These are compilation and synthetic execution checks, not a claim of browser touch or screen-reader acceptance.

## Veil completion

The [remaining Cruiser station revision](cruiser-science-comms-captain.md) moves Veil to the standard Courier Support fit. One star operates it automatically; two stars adds enable/disable; three stars adds specialist reassignment and machinery settings. The apparatus uses a real local reserve, supply and cooling, and existing emission rules determine its concealment.
