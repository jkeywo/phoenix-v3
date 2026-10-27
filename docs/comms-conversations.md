# Comms conversations

Comms uses a human-seeking Console, preferring the Courier's Support operator (Engineering on the engineering fit). The Cruiser retains its Comms identity. Conversation UI and authority are shared across native and browser clients.

## Playing

Select a conversation on the left to read its transcript on the right. Hail initially lists discovered ships and stations. Named individuals become contacts through a conversation introduction; detecting a vessel does not reveal its personnel directory. Call the vessel and ask for Command or Engineering through its reception conversation. Direct exchanges with the same ship share one visible conversation: hails, named officers, rescue updates and reminders appear together under the known ship name. Speakers remain individually identified. Multi-ship conferences stay separate from direct ship conversations. A character uses the Comms machinery of their host entity. Both endpoints require working, enabled, fully powered equipment and mutual range. Link loss queues individual recipients' messages while scripts and deadlines continue.

Scripted answers appear below the transcript. Each pending question keeps its own choices; the first admitted eligible answer to that question wins. When several operations ask questions, they appear together with their own reply controls. Free text uses an eligible all-human exchange with that ship; it does not become input to an NPC script. Scripts control group membership; joining does not reveal earlier messages. On Screen shares the selected conversation live through the same exclusive publication mechanism as radar/navigation; it grants no controls to viewers.

Select **Comms — Local assistance** to exercise ship-first hailing, an authored reception choice, a scripted engineer introduction, delayed speech and group choices. Choose Engineering, then switch to its Comms Console. The demonstration uses the unarmed Communications fit; the second Courier can be another player ship or ordinary AI. Fly outside mutual Comms range to exercise deferred delivery.

## Shared ship history

Grouping uses the physical participant ships, not conversation titles, officer names or faction ownership. Different ships with the same display name remain distinct. Titles use only identities already known to the viewing crew. Messages retain their original sender, portrait and global order; selecting the ship marks its visible source histories read.

Authored dialogue programs retain separate bindings, timers, prompts, receipt IDs and continuation. The projected ship conversation carries bounded source IDs and per-choice conversation/prompt ownership so native, browser and remote clients route each answer to its actual operation. Conference membership and delivered-recipient checks still control visibility; joining a conference does not disclose earlier direct messages. Human text uses the writable crew channel when the selected history includes scripted NPC messages. Existing saves retain their original records; the combined view and canonical selection are rebuilt from those records without migrating or resetting them.

## Authoring

A fitted `kind: "comms"` System specifies `range_mm`, positive `energy_per_second`, and `power_store`. Combat ships also place it in a compartment. Its Controller owns a normal `system_enabled` command. The Console widget uses `kind: "conversations"` and `binding: "ship.conversations"`.

An entity's `comms` object supplies `default_conversation` and `characters`. Each character has a stable `id`, `name_key`, optional `portrait`, optional `default_conversation`, and `public` (default true). Character identities are qualified by their entity instance. A character without its own default inherits its entity's default. Detection discloses the vessel endpoint. Individuals require initial conversation membership or an authored `reveal`/`add_participant` introduction, regardless of the retained legacy `public` annotation. Introduced identities persist normally and can subsequently be hailed directly.

Conversation trees use `context: "conversation"` with the ordinary states, entry/running/exit tasks, transitions, inheritance and serialisable continuation. Additional `lines` and `prompts` objects contain named dialogue definitions. Actions reference those names rather than embedding presentation strings in simulation code.

A line has `speaker` (participant binding), `text_key`, `delay_ticks`, `incoming_ticks`, and optional `expires_after`. Durations use fixed simulation ticks. Delay is silent; the incoming interval precedes delivery. A disconnected recipient sees no incoming indicator. Reconnection does not replay elapsed animation.

A prompt has `speaker`, `text_key`, and 1–16 `choices`, each with `id` and `text_key`. Optional `ai_choice` and `ai_delay_ticks` specify an unattended answer; `timeout_ticks` requires `default_choice`. No timeout means the prompt waits. Only one unresolved prompt exists per conversation. Prompt acceptance requires delivered evidence, current membership and a viable return link.

Available observations are `tick`, `answer`, `respondent`, `event`, `hails` and `prompt_pending`. Actions each take one text argument:

- `say`: line definition.
- `offer`: prompt definition.
- `add_participant`, `remove_participant`, `reveal`: participant binding.
- `emit`: declared scenario event.
- `finish`: descriptive reason; ends tree execution without deleting history or pending deliveries.

Default hails bind `caller` and `target`, plus the target entity's character IDs. Hailing a human-operated entity opens a human chat without executing its NPC default. Character targets remain authored NPCs.

Maps can define `conversations` with `id`, `tree`, `bindings`, `members`, optional `title_key`, `start_event` and `events`. Bindings identify `{ "entity": 1 }` or `{ "slot": "courier" }`, optionally with `character`. Members name initial bindings; other bound people can join through script actions. An omitted start event begins the conversation on the first tick with available participants. Subscribed declared scenario events enter the conversation's ordered event queue. Emitted conversation events return through the scenario runtime.

Workshop exposes Comms equipment, character directories, map conversation configurations and conversation state trees. Dialogue line/prompt fields use structured property editing within ordinary Undo/Redo, Verify and isolated Test.

## State and boundaries

Save schema 13 preserves threads, participant membership, recipient-specific delivery/expiry, prompt resolutions, script continuation, selections, read markers and scenario-event cursors. Older runtime schemas are refused explicitly. Existing packs without Comms remain valid.

Player projections contain only disclosed identities, delivered text, permitted choices and incoming status. The public display strips reply controls and other conversations. Projection truncation reports omitted historical messages; it does not discard authoritative deliveries or pending choices. There is no bandwidth gameplay meter.

This increment excludes audio/video, relays, solid-obstruction/field attenuation, encryption, psychic interference, player-created groups, personnel transport and generated NPC prose. Physical device, screen-reader and performance acceptance requires separate actual runs.

## Verification

Verified on Windows, 21 October 2026, on `codex/comms-conversations` based on `combat-mvp`:

- All 24 native system integrations pass. The Comms fixture uses the production pack loader and Controller-owned equipment, with timing, repeated hails, group-answer arbitration, free-text restrictions, stale authority, disabled equipment, queued/expired deliveries, introductions without historical disclosure, triggered dialogue, AI/default answers, invalid-save rejection and exact continuation.
- The complete WebAssembly gate passes, including native/WASM Comms and combat continuation comparisons. Final presentation changes are also built into the browser client/server targets; the Comms fixture runs under Node/WASM.
- Eight real-network cases were exercised. Seven passed in the full run; the live-session case timed out during concurrent builds and passed on its focused retry. The recovery fixture now checks save schema 11.
- The manually invoked `phoenix_comms_smoke` uses the authored local-assistance map, ordinary Console actions and the real RmlUi renderer at 1280×900 and 600×900. It covers public-character hailing, incoming indication, multiple-choice replies, Engineer Orin joining and replying, read-only On Screen publication, semantic transcript rows and narrow layout. Screenshots and a saved diagnostic state are written to the working directory.

Reproduce the visual journey separately from automatic tests:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_comms_smoke
Push-Location build/native
./Release/phoenix_comms_smoke.exe .
Pop-Location
```

Local evidence is in `build/comms-ctest-final.log`, `build/comms-web-check.log`, `build/comms-network-final.log`, `build/comms-network-focused2.log`, `build/comms-smoke.log`, and `build/native/comms-*.png`. These are run receipts, not production-content goldens. Browser graphical playthroughs, actual screen-reader/device operation and six-display performance are separate acceptance work.
