# Comic message presentation

Player Console and shared Viewscreen messages use the same presentation model: a sender name, portrait and automatically wrapping speech bubble. The Viewscreen consumes explicitly published critical AI emergency reports only. Neither visual prose nor accessible message names append technical routing IDs, tick counters, payload dumps or actionability labels. These fields remain in the underlying projections for admission, history and diagnostics.

Human message commands snapshot the sending lobby player's name and avatar. The snapshot travels through command serialization, delayed delivery and saved history, so later handovers or profile changes do not rename old messages. Legacy messages without a profile use their owning Console identity. Controller and physical reporter messages use the owning Console's authored, localised name.

The six AI portraits are recovered originals from the earlier built-in imagegen session, before the naval-uniform edits. `content/ui/messages/portraits.json` records source filenames and hashes. They are 192px display copies. Built-in mappings are Helm → 1, Engineering → 2, Science/Operations → 3, Comms/Communications/Support/Mission → 4, Weapons/Tactical → 5, and Captain/Command → 6. Custom Console IDs use a stable FNV-1a hash into the same six portraits. Missing or invalid player avatars use the Console portrait with the player's readable name.

Comic Neue Regular supplies bubble text. The unmodified font and its SIL Open Font License are bundled for native and browser use; existing multilingual fallback fonts remain loaded. Komika Text was preferred, but a verifiable bundled licence was not obtained, so the agreed OFL alternative is used. Missing translation keys or incomplete placeholders yield readable fallback text.

Message rows retain their element identity while new messages arrive. The existing containing panel scrolls; bubbles grow with text and have no manual resize controls. The message renderer does not impose the old additional eight-message Viewscreen limit. Authoritative history and network transfer limits still apply.

## Officer routing and trigger ownership

AI recipients receive relevant human, AI and factual-reporter messages. Humans receive AI officer messages and explicitly addressed equipment alerts. Ordinary physical reports remain evidence for AI. Human-to-human action reports are left to the people. Sender identity and origin are snapshotted at creation, and recipient operation is checked at delivery. An AI can report a need to a human-controlled department without approval buttons, continuing within its available resources.

Messages do not ask humans to accept assignments, approve AI actions or answer internal decision prompts. AI handles delegated work; humans use Command and ordinary machinery controls, and external conversations remain in Comms. Reports do not grant command authority or imply physical completion from command acceptance.

`emergency: true` enables shared publication for AI-origin messages and explicitly marked factual equipment alerts. Urgency controls attention only. A public report still requires a publishable schema and payload. Direct equipment emergencies appear on the Viewscreen without an additional Console popup. Messaging continuation is version 4.

Reports are authored through normal state trees and shared widget policies. Existing System reporters, effect outcomes and scoped read models provide their evidence. `report_observations: true` opts a policy into summary observations; these helpers neither trigger reports nor operate machinery. Ordinary referenced trees own the stable-condition delay, report/recovery transitions and private continuation. Native coordination lifecycle messages reuse the same admission and transport. No independent polling notification engine is added.

Automatic report templates are not player commands. Their `operator_report` metadata records the effective owner of the underlying work, using explicitly named native `source_operations` where applicable. The same reporting tree can therefore deliver human-work reports to AI without displaying them to humans. AI action outcomes retain AI provenance. Mixed Controllers use their actual native-operation or physical-command responsibilities rather than assuming that a staffed Console is wholly human.

Message destinations can name the sender's Console with `id: "self"`. Explicit Console `fallback` authoring supports different ship department arrangements without guessing names. Named recipients are verified against the actual fitting and schema permissions. Forwarded payloads contain only the reported evidence, not the source Console's other records.

## Messages popup and arrivals

The Messages button toggles a scrollable history popup across the player's currently granted Consoles, with a recipient filter and unread count. It does not pause play. Escape/Back closes it and returns focus to the button. Viewing entries marks them read independently of authoritative AI inbox acknowledgement.

New arrivals queue as non-blocking portrait bubbles over the active tab. They default to eight seconds, with duration choices in local Display settings, and pause while hovered/focused or while history is open. Activating any part of a bubble dismisses it and marks it read. The Messages button opens retained history. Expiring a bubble does not mark it read. Urgent arrivals use a short non-voice cue; local Audio settings can mute message cues. Simultaneous urgent arrivals share one cue. Initial history loading/reconnect establishes a silent baseline, and loss of Console access removes its messages from the current view.

Read state and popup browsing are local presentation state. Authoritative history survives saves and handover. Arrival announcements use the same readable message text without moving focus; scaling, ordinary controls and localisation follow the existing Console paths.

## Base pack routing audit

| Family                                            | Trigger, recipient and cadence                                                                                                                                                                                                                                                                               |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Addressed power, cooling and Engineering requests | An admitted request goes to the owning Controller. Acceptance, refusal and evidenced completion refer to that request. The typed receipt carries its identity.                                                                                                                                               |
| Thermal and power                                 | The fitted distributor reports sustained shortfalls to affected equipment Controllers. An allocation cut can trigger a direct Viewscreen rationing alert. The generator sends sustained overheating to Controllers that own cooling equipment fitted to its thermal store. Recovery stays on the blackboard. |
| Navigation and docking                            | Main engine, lateral thrusters, steering, boost, impulse, docking and displacement report their state through the blackboard. FTL sends a charge-start alert to its fitted power allocator.                                                                                                                  |
| Reserve, choir and Dispatch                       | These systems use the blackboard. Away mission conversations are authored through Comms.                                                                                                                                                                                                                     |
| Shields                                           | Low strength alerts fitted steering Controllers. Failure publishes directly to the Viewscreen. Strength and recovery remain on the blackboard.                                                                                                                                                               |

The base pack’s legacy automatic Controller reports and scored contact forwards are retired. Authored Controller requests, receipts and explicit evidence sends still use the coordination path.

Team completion notices use the committed clinical or repair outcome. A Medical team addresses the officer's current duty Controller when treatment restores them to health. A repair team addresses Controllers with fitted command or operation bindings to the equipment when its final active fault clears. The team is the factual sender. The authored templates supply the prose and the typed payload identifies the service and affected officer or equipment. The ordinary message queue persists pending delivery across saves.

Facilities with an authored `reporting_console` have their own factual sender identity. A recovery notice uses the officer’s current duty Controller or the Controller responsible for the duty’s fitted team.

Radar sends newly detected incoming missile alerts to fitted point-defence Controllers. The five-second cooldown is saved per receiving Controller and shared by overlapping radars. Contact tracks still update during the cooldown. Ordinary sensor evidence goes to the relevant fitted Controllers’ blackboards.

Weapon stock alerts use the selected fitted magazine and distinguish low ammunition from an empty load. The low threshold is one quarter of the store’s capacity for that resource. A blocked reload exposes its cause without granting the weapon access to reserve quantities. General stores send a shortage notice for a same-ship active request with fitted source and receiving Controller bindings.

Craft bay alerts cover a committed launch blocked by an unavailable bay. Host radios use the last confirmed link to report loss of contact with a deployed sortie. Scanner alerts require a matching outstanding scan request. An authored Comms line can set `alert_operation` to address the unique foreign Controller with that fitted responsibility. Routine transmissions remain in the Comms inbox.

## Validation

`system.gameplay_knowledge` exercises synthetic content through the production loader and World, checking delayed sender snapshots, save/restore and handover, prose-only semantics, private/public filtering, stable Console portraits, invalid-avatar fallback and missing translations. `system.controller_runtime` checks actual public-message publication and the separate public presentation model.

The explicitly invoked `phoenix_messages_smoke` target renders the production message component at wide and narrow sizes, including long text, enlarged type, multilingual fallback and escaped markup. It checks unchanged row identity, focus and scroll after arrival and captures `messages-wide.png`, `messages-narrow.png` and `messages-scrolled.png`. It is excluded from ordinary builds and CTest. A rendered/semantic probe does not establish actual screen-reader or physical-device acceptance.

`phoenix_officer_messages_smoke` is a separate explicitly invoked native journey through synthetic production content, delayed AI reports and the real Console surface. It checks popup toggling, an urgent cue, retained history, enlarged narrow layout, unique semantic controls and Back/focus restoration, and captures `officer-messages-arrival.png`, `officer-messages-wide.png` and `officer-messages-narrow.png`. It is excluded from ordinary builds and CTest.

On 30 October 2026 the isolated Release native build and production build check passed, both bundled packs verified, and seven affected integrations passed: gameplay knowledge, Controller runtime, gameplay foundations, Medical, content/packs, input/semantics and network content. All 38 launcher checks passed. The explicitly invoked popup journey passed at 1280×720 and 390×780 with 150% text; its three captures were visually inspected. Ordinary WebAssembly client/server builds passed with development linking, as did the gameplay-knowledge, Controller and Medical integrations under Node. Controller and Medical continuation hashes matched native exactly. These checks do not establish browser graphical, physical screen-reader, audio-device or six-display acceptance.

On 4 November 2026, gameplay knowledge, Medical, Controller runtime and Conversations passed on native Release and separately in WebAssembly under Node. The radar cooldown fixture covered overlapping senders, save and restore, and the exact expiry boundary. The native graphical smoke clicked the arrival text and portrait, checked unread counts, and reopened the retained entries. Workshop verified the base source, the reloaded source ZIP and the reloaded exported pack. Physical devices and screen readers were outside this run.
