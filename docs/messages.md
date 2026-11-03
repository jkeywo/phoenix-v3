# Comic message presentation

Player Console and shared Viewscreen messages use the same presentation model: a sender name, portrait and automatically wrapping speech bubble. The Viewscreen consumes explicitly published critical AI emergency reports only. Neither visual prose nor accessible message names append technical routing IDs, tick counters, payload dumps or actionability labels. These fields remain in the underlying projections for admission, history and diagnostics.

Human message commands snapshot the sending lobby player's name and avatar. The snapshot travels through command serialization, delayed delivery and saved history, so later handovers or profile changes do not rename old messages. Legacy messages without a profile use their owning Console identity. Controller and physical reporter messages use the owning Console's authored, localised name.

The six AI portraits are recovered originals from the earlier built-in imagegen session, before the naval-uniform edits. `content/ui/messages/portraits.json` records source filenames and hashes. They are 192px display copies. Built-in mappings are Helm → 1, Engineering → 2, Science/Operations → 3, Comms/Communications/Support/Mission → 4, Weapons/Tactical → 5, and Captain/Command → 6. Custom Console IDs use a stable FNV-1a hash into the same six portraits. Missing or invalid player avatars use the Console portrait with the player's readable name.

Comic Neue Regular supplies bubble text. The unmodified font and its SIL Open Font License are bundled for native and browser use; existing multilingual fallback fonts remain loaded. Komika Text was preferred, but a verifiable bundled licence was not obtained, so the agreed OFL alternative is used. Missing translation keys or incomplete placeholders yield readable fallback text.

Message rows retain their element identity while new messages arrive. The existing containing panel scrolls; bubbles grow with text and have no manual resize controls. The message renderer does not impose the old additional eight-message Viewscreen limit. Authoritative history and network transfer limits still apply.

## Officer routing and trigger ownership

AI recipients receive relevant human, AI and factual-reporter messages. Human recipients see AI officer messages only. Physical reporters remain factual evidence for AI; they do not directly notify humans. Human-to-human action reports are left to the people. Sender identity/origin is snapshotted at creation, and recipient operation is checked at delivery. An AI can report a need to a human-controlled department without approval buttons, continuing within its available resources.

Messages do not ask humans to accept assignments, approve AI actions or answer internal decision prompts. AI handles delegated work; humans use Command and ordinary machinery controls, and external conversations remain in Comms. Reports do not grant command authority or imply physical completion from command acceptance.

`emergency: true` explicitly enables shared publication for AI-origin messages. Urgency controls attention only. A public report still requires a publishable schema/payload. The former automatic publication of urgent AI-to-AI messages is removed. Messaging continuation is version 4; older messaging saves require a new session and are preserved rather than rewritten.

Reports are authored through normal state trees and shared widget policies. Existing System reporters, effect outcomes and scoped read models provide their evidence. `report_observations: true` opts a policy into summary observations; these helpers neither trigger reports nor operate machinery. Ordinary referenced trees own the stable-condition delay, report/recovery transitions and private continuation. Native coordination lifecycle messages reuse the same admission and transport. No independent polling notification engine is added.

Automatic report templates are not player commands. Their `operator_report` metadata records the effective owner of the underlying work, using explicitly named native `source_operations` where applicable. The same reporting tree can therefore deliver human-work reports to AI without displaying them to humans. AI action outcomes retain AI provenance. Mixed Controllers use their actual native-operation or physical-command responsibilities rather than assuming that a staffed Console is wholly human.

| Trigger family                         | Production source and report behaviour                                                                                                                                                                                                    |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Power, cooling and thermal constraints | Owned machinery telemetry and coordination commitments; report needs, limits and recovery. Thermal/supply/repair reports can forward to Engineering, explicitly falling back to Support.                                                  |
| Repairs and Medical                    | Permitted repair board and owned team work supply grouped needs, team blockages and clinical completion. Medical inspections, patient alerts and clinical outcomes stay on the Medical Console, including Support where it owns the work. |
| Navigation and docking                 | Existing navigation assessment, admitted docking state and shared action outcome branches; report sustained obstruction, destination reached/stopped, follow contact lost and docking completion. Routine lateral corrections are silent. |
| Tactical                               | Permitted hostile contacts, actual intercept candidates and owned weapon ammunition; group threats and report ammunition constraints. No per-shot/interception chatter.                                                                   |
| Craft                                  | Owned bay's permitted craft reports; report deployment, losses, link loss/recovery and blocked operations. Losing a link never proves destruction.                                                                                        |
| Science                                | Scoped sensor findings and scanner blockers; report consequential findings and interruptions. Findings can forward to Comms, explicitly falling back to Support. Ordinary identity updates stay in contact details.                       |
| Shields and psychic work               | Owned shield state and scoped choir evidence; report depleted protection and inability to sustain enabled work, with recovery.                                                                                                            |
| Away operations and FTL                | Scoped mission phases/opportunities and FTL readiness/blockers; report actual outcomes. A supply promise is not delivered cargo, and departure remains deliberate.                                                                        |

Message destinations can name the sender's Console with `id: "self"`. Explicit Console `fallback` authoring supports different ship department arrangements without guessing names. Named recipients are verified against the actual fitting and schema permissions. Forwarded payloads contain only the reported evidence, not the source Console's other records.

## Messages popup and arrivals

The Messages button toggles a scrollable history popup across the player's currently granted Consoles, with a recipient filter and unread count. It does not pause play. Escape/Back closes it and returns focus to the button. Viewing entries marks them read independently of authoritative AI inbox acknowledgement.

New arrivals queue as non-blocking portrait bubbles over the active tab. They default to eight seconds, with duration choices in local Display settings, and pause while hovered/focused or while history is open. Activating any part of a bubble dismisses it and marks it read. The Messages button opens retained history. Expiring a bubble does not mark it read. Urgent arrivals use a short non-voice cue; local Audio settings can mute message cues. Simultaneous urgent arrivals share one cue. Initial history loading/reconnect establishes a silent baseline, and loss of Console access removes its messages from the current view.

Read state and popup browsing are local presentation state. Authoritative history survives saves and handover. Arrival announcements use the same readable message text without moving focus; scaling, ordinary controls and localisation follow the existing Console paths.

## Base pack routing audit

| Family | Trigger, recipient and cadence |
| --- | --- |
| Addressed power, cooling and Engineering requests | An admitted request goes to the owning Controller. Acceptance, refusal and evidenced completion refer to that request. The typed receipt carries its identity. |
| Thermal and supply | Sustained constraints and recovery go to the source Console and can forward to Engineering or its Support fallback. The policies skip forwarding from those recipient Consoles. The legacy generator protection reporter now routes only to Engineering and describes thermal recovery. |
| Repair and Medical | The assigned team reports significant needs, blockage and clinical completion through its scoped board. Patient notices open an episode when the local count rises from zero, with no extra notice for each increase. Routine inspections and withdrawals for both departments stay off Helm and Captain. Machinery constraints and recovery still go to the Console operating the affected equipment. |
| Contact evidence and scan findings | Helm and Tactical retain scoped contact forwards for decision evidence. Consequential Science findings can forward to Comms or Support, with self-forwarding guarded. Presentation uses readable text while structured evidence retains its provenance and coordinates. |
| Navigation, docking and FTL | Sustained course or drive blockers, recovery and admitted action outcomes go to the affected Console. Routine lateral correction stays silent. |
| Tactical, shield and psychic | Threat grouping, ammunition and protective-system constraints report to their operating Console, with state transitions for recovery. |
| Craft and away work | Dispatch and operating Consoles receive material mission and link changes. Scoped reports distinguish lost links, losses and physical outcomes. |
| Legacy ship-wide advisories | The base pack declares the old supply-limited, navigation-advisory and lateral-avoidance templates without active sends. |

The retained scored evidence routes use per-record and per-route cooldowns. A delivery that refreshes the same typed evidence remains available to AI without another human notification or history entry. A changed claim or withdrawal still appears. Public emergency reports retain their existing presentation.

Service completion notices use the committed clinical or repair outcome. Medical addresses the officer's current duty Console when treatment or facility recovery restores them to health. Repair addresses the Consoles with fitted command or operation bindings to the equipment when its final active fault clears. Telemetry feeds alone do not subscribe a Console. The service Controller supplies the sender and its effective operation supplies the origin. The authored templates supply the prose and the typed payload identifies the service and the affected officer or equipment. The ordinary message queue persists pending delivery across saves.

## Validation

`system.gameplay_knowledge` exercises synthetic content through the production loader and World, checking delayed sender snapshots, save/restore and handover, prose-only semantics, private/public filtering, stable Console portraits, invalid-avatar fallback and missing translations. `system.controller_runtime` checks actual public-message publication and the separate public presentation model.

The explicitly invoked `phoenix_messages_smoke` target renders the production message component at wide and narrow sizes, including long text, enlarged type, multilingual fallback and escaped markup. It checks unchanged row identity, focus and scroll after arrival and captures `messages-wide.png`, `messages-narrow.png` and `messages-scrolled.png`. It is excluded from ordinary builds and CTest. A rendered/semantic probe does not establish actual screen-reader or physical-device acceptance.

`phoenix_officer_messages_smoke` is a separate explicitly invoked native journey through synthetic production content, delayed AI reports and the real Console surface. It checks popup toggling, an urgent cue, retained history, enlarged narrow layout, unique semantic controls and Back/focus restoration, and captures `officer-messages-arrival.png`, `officer-messages-wide.png` and `officer-messages-narrow.png`. It is excluded from ordinary builds and CTest.

On 30 October 2026 the isolated Release native build and production build check passed, both bundled packs verified, and seven affected integrations passed: gameplay knowledge, Controller runtime, gameplay foundations, Medical, content/packs, input/semantics and network content. All 38 launcher checks passed. The explicitly invoked popup journey passed at 1280×720 and 390×780 with 150% text; its three captures were visually inspected. Ordinary WebAssembly client/server builds passed with development linking, as did the gameplay-knowledge, Controller and Medical integrations under Node. Controller and Medical continuation hashes matched native exactly. These checks do not establish browser graphical, physical screen-reader, audio-device or six-display acceptance.
