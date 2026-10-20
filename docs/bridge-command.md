# Alert conditions, Command and the Viewscreen sidebar

Ships author `bridge.conditions` as an ordered list, from least to most severe. Each condition has an `id`, localised `label`, optional `description`, `colour` in `#RRGGBB`, `glow`, and optional pack-qualified OGG `siren` and `music` references. There is no fixed five-condition rule. `bridge.initial` selects the initial ID; omitting it selects the first condition. `bridge.alert_console` identifies the Console authorised to set the condition.

The base ships have conditions 5 through 1, starting at 5. Condition 1 uses the V2 siren. Courier Engineering/Support owns alert selection; the Cruiser Captain owns it. Alert selection is independent of Command.

Accepted condition ID, severity (zero-based list position) and revision are shipwide blackboard state. Console projections and the Viewscreen expose the same state. Controller trees read `alert.id` (text), `alert.severity` (integer), and `console.stance` (text). Unauthored values are absent. Saving retains accepted state; selecting the current condition does not increment its revision.

The ship server sounds an authored siren once on a live condition change. Initial snapshots and recovery are silent. A condition's music loops until selection changes; absent music uses ordinary mission music. Music, siren, interface SFX and master volumes are separately controlled. Static edge glows accompany a textual, accessible condition readout. The Viewscreen and each Console receive their ship's glow, including remote Consoles.

## Console stances

A Console may author `stances`, each with an `id`, localised `label` and optional `description`, and `default_stance`. These are named AI inputs, not actuator presets. Its own operator may change the stance even while some Controllers are delegated. A `command: true` Console may select another Console's stance only when all of that Console's Controllers are AI-operated. Admission and execution both check this, and target authority generations invalidate queued orders across handovers.

Command defaults in shipped content to `human_seeking: true`. `hide_without_ai: true` hides its supplemental presentation when no eligible stance targets remain. Command itself and Consoles without stance choices are excluded. Helm's Follow orders and Hold position stances demonstrate tree-driven behaviour: delegated propulsion can follow objectives or cancel destination guidance and request a stop. Human-owned propulsion remains under its operator's control.

Engineering supplies Combat Power, Normal Power and Recharge Power. Combat requests maximum safe generation and full authored operating allocations. Normal follows the previous committed generator-store load (exports, consumption and losses), bounded by the fitted maximum and the operator’s generation limit. Recharge requests maximum safe generation while reducing nonessential distribution allocations to a quarter of their normal values, leaving core services available so surplus energy replenishes the reserve. Thermal protection retains priority in every mode; stances only affect AI-owned Controllers. The existing routine and reserve identities remain compatible with saved selections.

## Viewscreen placement

Human-seeking Consoles opt in with `viewscreen: true` and a `compact.widgets` layout. The host selects one eligible Console beside the Viewscreen; it occupies the right quarter and the scene and shared map use the remaining width. The sidebar has a separate routed Console context and an exclusive claim. Another human's Console cannot be silently taken.

`bridge.sidebar_default` reserves a supplemental Console for the Viewscreen host instead of assigning it implicitly to a station. Both Couriers default to Command. Auto-hiding releases the claim and restores the scene's full width; an automatically hidden default can return when AI targets return. Explicitly closing or choosing another Console suppresses automatic default selection for that session. Navigation also has an authored compact layout.

Sidebar selection and **Close sidebar** live in the Viewscreen's **Settings → Session** menu. The sidebar shares the Viewscreen settings entry; it does not display a second cog. Settings retain the sidebar lease and route Console-specific controls to it. Closing explicitly releases that lease; selecting a Console in settings can reopen it.

The Viewscreen displays its authored condition label in an opaque industrial readout panel at the lower-left frame. It retains the shipwide edge glow and accessible condition text, without a duplicate sidebar readout.

Command groups eligible AI-operated Consoles into named, sprite-framed cards. Each card presents wrapping rows of compact stance buttons with selected and keyboard-focus states. These use the existing industrial nine-slice artwork. Command has no user-facing complexity profile; internal operator defaults remain compatible. Other Consoles expose complexity under **Settings → Session**, including before launch, while dedicated operational controls such as Helm's manual switch remain on the Console.

The sidebar uses the host-only reserved runtime route 31; it does not add a lobby seat or a physical display. Existing “On screen” publication remains independent. Private sidebar data does not enter the public Viewscreen projection.

## Authoring and verification

Workshop exposes ship condition lists, initial condition, alert owner and default sidebar, plus Console stances, Command visibility and compact HUD authoring. Definition loading checks choices, colours, audio references and Console placement. Synthetic Controller integration covers alert and stance admission, shared AI reads, handover refusal, ship isolation and checkpoint continuation. Audio transition tests use the same revision consumer as playback.

Rendered layout, audible output, actual screen-reader operation, physical touch/gamepad devices and six-display performance require separately invoked smoke checks; a successful build or synthetic test does not establish those results.

### Verification — 20 October 2026

Implementation commit `22be094` passed the final native build and all 22 native system cases. The production verifier accepted Base and Training together: 12 entity definitions, 8 maps and 9 Consoles. Formatting verification passed. The synthetic cases include host-only committed sidebar leasing, target-authority generations, auto-hide/reappearance, touch capture and controller neutralisation.

The complete WASM check passed, including the browser launcher, input/semantics, content, Controller runtime and save/recovery integrations, and comparison with the native continuation trace.

The network gate passed seven of eight cases on its initial run. The remaining live-session case timed out waiting for GM approval; its focused rerun passed. The copied siren's SHA-256 matches the V2 original. No rendered, audible or physical-device smoke run was performed for this change.

Integration into main retained the newer skybox and sprite-mirroring work. Additive conflicts retained both the skybox translations/delivery notes and the map-gesture/sidebar tests. The combined `scripts/check.ps1 -Target all` passed all 22 native cases, all 8 network cases and the WASM checks. Production content verification again accepted 12 entity definitions, 8 maps and 9 Consoles; formatting verification passed. This integration did not run additional graphical, audible or physical-device smoke checks.

### Explicit UI smoke

Run the native executable with normal local mission arguments plus --bridge-ui-smoke 1285 725 (also check 1920 1080 and 960 720). Use a Courier with Command as its default sidebar and an AI-operated stance target. The probe captures the Viewscreen, shared settings and selected stance under build/bridge-WIDTHxHEIGHT-*.png; it checks hidden toolbar/complexity controls, shared routing, close/reopen leases and stance invocation. This remains a manually invoked graphical smoke, not an automatic system gate or a substitute for actual reader/device testing.

### Layout cleanup verification — 20 October 2026

Native and browser server/client builds passed. The synthetic Controller integration passed on native and WASM, including Command's absent complexity controls; the native input/semantics, local-session and fleet-setup cases also passed. The production base verifier accepted 9 entities, 3 maps and 9 Consoles.

Explicit graphical probes passed at 1285×725, 1920×1080 and 960×720. Captures checked the framed condition, grouped stance buttons, selected state, shared settings, sidebar close/reopen and expanded localisation labels. No physical touch/gamepad or actual screen-reader run is claimed.

The condition housing is a raised extension of the bottom rail, assembled from the viewscreen frame sprite and joined beneath its outer rail. Embedded consoles continue the same top and bottom rails and share one divider. Command cards use fixed frame clearance; narrow consoles wrap stance buttons to the available card width.

Engineering power follow-up: all three authored packs verify with Normal, Recharge and Combat choices. The synthetic Controller check exercises typed, capped generator-load telemetry through the permitted observation path. Explicit captures at all three sizes confirm larger two-line English labels and wrapping expanded labels inside the frame.
