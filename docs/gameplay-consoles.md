# Gameplay Console presentation

`ConsoleModel` consumes the current routed Console's permitted `Observation`.
Its graphical and semantic controls share the same descriptors and command
admission path. The model never opens a world, merges other Consoles' boards or
reads a private AI continuation.

## Projected controls

The authoritative projection can supply this bounded authored control list:

```json
{
  "controls": [
    {
      "id": "drive-neutral",
      "label_key": "helm.neutral",
      "action": "longitudinal_thrust",
      "system": "drive",
      "value": 0
    }
  ]
}
```

`label` is also supported for an ordinary translated source label. An optional
`enabled:false` hides an unavailable action. Values are exact signed integers;
booleans use admitted numeric 0/1. Descriptor IDs are opaque, at most 256 bytes,
and must be unique within the current projection. Buttons use canonical
`game.control/<hex-encoded-id>` identifiers. Activation decodes the ID and resolves
the current descriptor again; command names, System IDs and values are never
parsed from a caller-provided semantic action string. Missing, duplicate, stale
and malformed descriptors fail safely.

`ConsoleModel::command(action, value, system)` supplies exact ordinary commands,
including signed manual axes. Known operating commands check the declared value
range and an owned System selection or explicit projected descriptor before
entering `SessionPort::console_command`.
Other supported actions, such as Away, complexity profiles, publication, messages
and summary intent, require an exact current projected descriptor. The server
remains the final authority for ownership, capability and operating limits.

Manual controls can explicitly set negative, neutral and positive input.
`hold_control`, `release_control` and `release_controls` retain the exact admitted
axis/System association and submit neutral on release. The routed RmlUi surface
uses these on press, release and cancellation; replacing its controls or losing
the routed pointer also releases held inputs. The authoritative handover/mode
boundary independently neutralises stale axes. A projected neutral button is
available to non-held keyboard and semantic operation. The Console model does not
invent simulated thrust from presentation.

Throttle fractions span `SystemView.telemetry.minimum_speed` through the fitted
maximum, with zero minimum for existing content. Increment/decrement buttons use
the same interval. A reverse-capable ship can therefore command reverse through
the same graphical and semantic interface. Heading retains its existing absolute
degrees contract.

## Readouts and knowledge

`gameplay_controls()` and `gameplay_readouts()` return stable semantic IDs and
localised text. The routed `ConsoleSurface` and legacy `GameScreen` render the same
values through RmlUi and register them with the existing navigation/reader
projection. The forward Viewscreen receives none of these operating controls.

The routed panel defaults to the right of authored widgets on wide screens and
below them on narrow screens, with its own scrolling region. A Console without
widgets gives the panel the available body area. Authors may supply
`gameplay_panel:{x,y,width,height}` using 0..1 coordinates relative to the body
below the toolbar; this explicit placement requires authors to reserve the
corresponding widget space. Controls and readouts update independently so a new
report does not recreate focused operating buttons. Legacy presentation uses a
bounded scrolling panel above its widgets.

System readouts distinguish requested settings, projected numeric telemetry and
the current limitation/status reason. Console and shipwide records retain source,
observation time, uncertainty, fact/claim/intent/prediction kind and withdrawal.
Staleness derives from the supplied observation tick and schema
`stale_after_ticks`; displaying a record never refreshes its evidence time.

The model selects the newest version for each permitted
schema/subject/source/publication stream. It does not combine private fields from
other streams. Published objectives include their localised title, status,
destination and credited state. Message history includes sender/addressee,
delivery tick, urgency, actionable/historical state, semantic payload and evidence
provenance. All values are text-escaped before RmlUi insertion.

Messages use `title_key`, `body_key` and `parameters`. The model translates the
template first, then substitutes `{parameter}` tokens once. A substitution value
cannot create another template expansion or executable markup. Payload values
remain separate from this presentation.

## Fresh notifications

`Observation.gameplay.notifications` contains only deliveries that were eligible
to notify a human, with `sequence`, translation keys, parameters and urgency.
History is not used to infer notification eligibility. The first observation is
a baseline, so opening a Console or reconnecting does not announce historical
messages. Later delivery sequences update the announcement once; repeated
projection refreshes do not retrigger the same sequence. Changing routed Console,
session generation or pack identity establishes a fresh baseline.

This presentation work supplies ordinary graphical and semantic information. It
does not establish audible screen-reader, physical touch/controller or six-screen
performance acceptance without the corresponding manually triggered smoke runs.

## Manual control

Helm's radar also includes one **Manual** toggle on its lower-left rim. Green/round/plus/On means manual control; red/outlined square/minus/Off means the automatic profile. It uses the Console's accepted profile projection and the normal admitted preset command, so pending requests do not change the indicator. Turning manual control back on restores the last observed manual complexity at that Console during the current session; a session starting in automatic mode uses the first admitted manual profile. Consoles without an authored `automatic` profile retain their profile controls. Settings still exposes the detailed complexity choices. Pointer/touch and keyboard Enter activate the whole switch; gamepad Back/Select focuses it, South/A activates it and East/B leaves focus. Its semantic toggle exposes Manual On/Off.

## Shared camera widgets

`camera_mode` and `camera_direction` widgets change the shared ship camera through exact projected commands and the normal Console admission path. Their bindings are `ship.camera.mode` and `ship.camera.direction`; neither requires a physical System or Controller channel. Their selection follows the accepted `phoenix.camera` ship-blackboard record. Workshop supports placement and isolated preview on any Console, and shipped Helm layouts include both. See [camera behavior, input and persistence](ship-camera.md).

## Manual Helm control

Helm offers an **Assisted flight / Manual flight** toggle when its authored speed and heading widgets bind human-owned inertial flight Controllers with compatible manual axes. The Courier's movement console uses the same control. Assisted flight remains the default. The mode only displays Manual after both propulsion and steering mode commands have been consumed and accepted.

Manual flight uses **W/S** for forward/reverse thrust and **A/D** for left/right yaw. The explicitly selected Console receives keyboard input; text entry and settings suspend it. The assigned gamepad's left stick and the on-screen spring-centred joystick use horizontal yaw and vertical forward/reverse thrust. Each gamepad axis has a 15% dead zone, rescaled over the remaining travel. Both axes can reach full effort simultaneously. Reverse follows fitted engine capability.

Hold input to request proportional forward/reverse or lateral speed; release requests zero and passively brakes toward rest. Turning changes the desired velocity with actual hull attitude without instantly rotating existing velocity. Passive damping corrects drift without power or heat; powered acceleration still respects thermal, damage and propulsion limits. Returning to assistance captures current forward speed and attitude. Entering manual clears active navigation through the existing flight mode path. Lateral fittings retain their existing controls.

One input source owns manual flight until neutral. Changed axis pairs are submitted at most once per simulation tick, with immediate neutral releases. Another held source must return to neutral before taking over. Focus loss, settings, pointer cancellation, reassignment, device removal and authority changes release held input. Gamepads must centre both axes after assignment or focus recovery. Keyboard input must be released before resuming after an interruption. Held controls are transient and are never restored from UI state.

The toggle, mode and axis readout share native/browser semantics. Labelled yaw/thrust pulse buttons provide discrete keyboard and screen-reader actions; each pulse releases after the next simulation tick. **Release flight controls** clears current held input. UI requests use the normal Console admission path and its current Controller generation; no presentation control writes physical state directly.

See [manual Helm verification](smoke/manual-helm.md) for automated checks and separately invoked graphical/device evidence.
