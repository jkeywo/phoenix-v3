# Alert conditions, Command and the Viewscreen sidebar

Ships author `bridge.conditions` as an ordered list, from least to most severe. Each condition has an `id`, localised `label`, optional `description`, `colour` in `#RRGGBB`, `glow`, and optional pack-qualified OGG `siren` and `music` references. There is no fixed five-condition rule. `bridge.initial` selects the initial ID; omitting it selects the first condition. `bridge.alert_console` identifies the Console authorised to set the condition.

The base ships have conditions 5 through 1, starting at 5. Condition 1 uses the V2 siren. Courier Engineering/Support owns alert selection; the Cruiser Captain owns it. Alert selection is independent of Command.

Accepted condition ID, severity (zero-based list position) and revision are shipwide blackboard state. Console projections and the Viewscreen expose the same state. Controller trees read `alert.id` (text), `alert.severity` (integer), and `console.stance` (text). Unauthored values are absent. Saving retains accepted state; selecting the current condition does not increment its revision.

The ship server sounds an authored siren once on a live condition change. Initial snapshots and recovery are silent. A condition's music loops until selection changes; absent music uses ordinary mission music. Music, siren, interface SFX and master volumes are separately controlled. Static edge glows accompany a textual, accessible condition readout. The Viewscreen and each Console receive their ship's glow, including remote Consoles.

## Console stances

A Console may author `stances`, each with an `id`, localised `label` and optional `description`, and `default_stance`. These are named AI inputs, not actuator presets. Its own operator may change the stance even while some Controllers are delegated. A `command: true` Console may select another Console's stance only when all of that Console's Controllers are AI-operated. Admission and execution both check this, and target authority generations invalidate queued orders across handovers.

Command defaults in shipped content to `human_seeking: true`. `hide_without_ai: true` hides its supplemental presentation when no eligible stance targets remain. Command itself and Consoles without stance choices are excluded. Helm's Follow orders and Hold position stances demonstrate tree-driven behaviour: delegated propulsion can follow objectives or cancel destination guidance and request a stop. Human-owned propulsion remains under its operator's control.

## Viewscreen placement

Human-seeking Consoles opt in with `viewscreen: true` and a `compact.widgets` layout. The host selects one eligible Console beside the Viewscreen; it occupies the right quarter and the scene and shared map use the remaining width. The sidebar has a separate routed Console context and an exclusive claim. Another human's Console cannot be silently taken.

`bridge.sidebar_default` reserves a supplemental Console for the Viewscreen host instead of assigning it implicitly to a station. Both Couriers default to Command. Auto-hiding releases the claim and restores the scene's full width; an automatically hidden default can return when AI targets return. Explicitly closing or choosing another Console suppresses automatic default selection for that session. Navigation also has an authored compact layout.

The sidebar uses the host-only reserved runtime route 31; it does not add a lobby seat or a physical display. Existing “On screen” publication remains independent. Private sidebar data does not enter the public Viewscreen projection.

## Authoring and verification

Workshop exposes ship condition lists, initial condition, alert owner and default sidebar, plus Console stances, Command visibility and compact HUD authoring. Definition loading checks choices, colours, audio references and Console placement. Synthetic Controller integration covers alert and stance admission, shared AI reads, handover refusal, ship isolation and checkpoint continuation. Audio transition tests use the same revision consumer as playback.

Rendered layout, audible output, actual screen-reader operation, physical touch/gamepad devices and six-display performance require separately invoked smoke checks; a successful build or synthetic test does not establish those results.
