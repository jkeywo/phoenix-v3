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
Other supported actions, such as Away, delegation, presets, publication, messages
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
