# Shared Controller actions and officer requests

Controller policies select reusable execution trees. Execution trees use the same owned command channels, permitted Console observations and physical machinery as player controls. There is one active action per Controller; other Controllers continue independently. Conversation and away-operation trees keep their existing contexts.

## Authoring an action

A Controller-context tree becomes an action definition by declaring `controller_action` with a localisable `label`, typed `inputs`, and optional `available` conditions. Input types are `integer`, `boolean` and `text`. Execution tasks read immutable invocation values through `input.<name>`. Ordinary tree `parameters` remain compile-time configuration.

```json
{
  "id": "example.maintain-speed",
  "context": "controller",
  "controller_action": {
    "label": "Maintain speed",
    "inputs": [{ "name": "speed", "type": "integer" }]
  },
  "command_bindings": [{ "channel": "speed" }],
  "initial": "operate",
  "states": [{
    "id": "operate",
    "running": [{ "action": "write.speed", "arguments": [{ "observation": "input.speed" }] }]
  }]
}
```

The selecting policy declares `action_bindings`, for example:

```json
{
  "id": "cruise",
  "tree": "example.maintain-speed",
  "channels": { "speed": "port-speed" }
}
```

Its own command bindings must expose the actual owned `port-speed` channel. `channels` maps execution-local command names to actual channels; `observations` can similarly map execution-local observation names to permitted policy observations. Bindings also apply to reusable execution subtrees. Missing capabilities, incompatible argument types and recursive calls fail compilation. Actions cannot call the top-level scheduler. Policies using actions cannot also issue direct writes; older direct-write policies remain supported separately.

`select.cruise(invocation, speed)` starts or continues work. Repeating the same selection does not restart it. Changed inputs replace it. `replace.cruise` explicitly starts a replacement for that selecting task activation. `action.cancel(reason)` interrupts the current executor. Policy activation is included in the provider's invocation identity, so re-entering an order state can issue fresh work without using evaluation ticks as identity.

Executors finish with `action.succeed(reason)` or `action.fail(reason)`. Status is `idle`, `running`, `succeeded`, `failed` or `cancelled`; terminal results remain until a new invocation. Sustained regulation, heading, follow and shielding actions remain running. Command refusal fails execution with `command_refused`. Exit tasks run on interruption, replacement and handover, using ordinary commands; they cannot undo completed physical effects. Held-input neutralisation remains part of Controller handover.

Completion must use observed results. A command admission receipt alone is not evidence that a destination was reached or equipment repaired. Execution observations include the same tick snapshot as policy observations. The navigation helpers `action.goal_distance`, `action.actual_speed` and `action.contact_available` use permitted position/contact evidence and the conventional `destination_x`, `destination_y` and `contact` inputs. Maintained destination actions finish on distance and speed conditions; follow actions fail safely when contact evidence disappears.

Console projections expose action identity, definition, input descriptors, availability and current status. Workshop's tree bindings editor edits action metadata and capability maps; its typed task palette includes invocation operations. Verify checks the resulting content, and isolated Test includes action and coordination state in existing Controller feedback. These are authoring/debug surfaces, not a new player action panel.

## Maintained policies

The base pack authors combat decisions as ordinary trees selecting shared engagement, approach, heading, repair, reactor and shield actions. Hostile candidate selection, distance arithmetic, ammunition suitability and repair candidates are bounded helpers over permitted observations. They do not write commands. Valid weapon targets and useful repair assignments are retained. Shield redistribution has authored entry/exit thresholds; engineering thermal recovery uses an authored dwell. Transit retains its destination under limited supply and changes its operating speed after an accepted, limited or refused response.

Older `policy: combat` content converts into shared policy/action trees during content preparation. There is no separate live C++ combat decision loop. Content dependency closure follows action and execution-subtree references; browser clients and Workshop remain free of authoritative execution code.

## Officer coordination

`coordination.request(operation, console, controller, target, amount, priority, lifetime)` sends an ordinary internal message. Supported operations are `power`, `cooling`, `manoeuvre` and `repair`. Priority is 0–2; lifetime is bounded to 1–72,000 ticks. Requests are limited to the sending ship and deduplicated while active. Manoeuvre requests require a currently permitted hostile; repair requests require observed compartment damage or a permitted equipment fault.

`coordination.respond(status, amount, reason)` correlates a reply to the selected incoming request. Status distinguishes `accepted`, `refused`, `completed` and `failed`. `coordination.withdraw(operation)` withdraws obsolete outgoing work. Transport acknowledgement only advances message delivery; it does not accept a duty. Reply facts are exposed as `coordination.reply.<operation>.status` and `.amount`.

Supported recipients use ordinary generation, coolant allocation, navigation or repair commands. Engineering bounds the accepted amount by its channel capacity and permitted operating limit. Navigation checks its evidence and existing orders; repair completion checks both compartment condition and remaining observed faults. Successful sustained supply/manoeuvre work remains a commitment until withdrawal, expiry or inability to continue.

Arbitration respects machinery protection and explicit stances/orders before coordination, then routine work. Equal-priority requests retain an existing commitment before arrival order and stable identity. `officer_routine_stance` can name the stance in which coordination is allowed; the default is `routine`. Handover cancels AI execution and reports/withdraws its commitments. Expired and terminal records are bounded and pruned.

Requests preserve their original subject and evidence scope. A repair request can name a compartment associated with an observed fault without exposing another Console's complete repair board. Human recipients see the same request identity, target and amount in existing internal messages. Maintained supply-message templates allow explicit acceptance, refusal and completion reports using that request number, alongside ordinary machinery controls. Silence and acknowledgement never become agreement.

## Fixed map factions

Maps may define `factions: [{id, label}]` and `relationships: [{a, b, relation}]`. Relations are mutual `friendly`, `neutral` or `hostile`. Same-faction relations are friendly; omitted pairs and unaffiliated entities are neutral. Unknown faction references and contradictory reverse entries are invalid.

Entity definitions may supply an optional default `faction`. Map placements and ship slots override it for that instance; an explicit empty string means unaffiliated. The active catalogue validates placements, ship choices and scenario-spawnable definitions. Maps without factions remain valid. The combat duel declares its opposing factions explicitly.

Detected contact projections carry faction ID, localisable label and viewer-relative disposition to both humans and AI. Contact details use text, not colour alone. Undetected contacts do not provide faction updates. Autonomous engagement excludes friendly, neutral and undetected entities; explicit player weapon authority is unchanged. Workshop edits map factions/relationships and entity, placement and slot faction values through existing history and verification.

## Persistence and verification

Empty idle action/coordination records are omitted from saves and restored as defaults, keeping ordinary worlds compact without dropping active or terminal outcomes.

World/session schema **12** persists action identity, inputs, continuation, outcome, effect receipts, officer commitments/replies and fixed faction membership. Incompatible runtime schemas/contracts are rejected transactionally; valid older content still uses defaults and boundary conversion. No external inference service or JEV adapter is included.

Synthetic production-loader integrations exercise shared actions with different policy/channel bindings, runtime argument types, replacement, completion, cleanup, forbidden writes, factions, hostile selection, supply/cooling exchanges, manoeuvre, repair completion and exact save/resume. The normal native, WASM and network gates remain the integrated acceptance checks. Graphical smoke, physical-device operation and screen-reader acceptance require separate explicitly recorded evidence.

### Verification receipt — 21 October 2026

- Native gate: all 26 system integrations and 19 launcher cases passed, including production content verification and the native build check.
- Network gate: all eight real-transport cases passed. The synthetic GM world was resized to remain within the unchanged 4 MiB inspection limit; assertions still require a multi-chunk snapshot larger than 64 KiB, full GM visibility and restricted crew visibility.
- WebAssembly gate: build, production launch preflight and system integrations passed. Native/WASM continuation comparisons passed for simulation, shared Controller actions and coordination, combat, conversations and away operations.
- Repository formatting check and `git diff --check` passed.
- No graphical smoke, physical-device or screen-reader acceptance was performed for this change.
