# Authoring Console Controllers

This describes the implemented explicit-layout contract. See [the architecture](controller-architecture.md) for the target and [the refactor plan](controller-refactor-plan.md) for migration status. Existing production definitions still using per-System policies have not yet been migrated by this document.

## Layout and commands

A controllable entity declares `control_layout` with `schema: 1`, a Controller list, complexity profiles and a default profile. A Controller's identity is its entity, Console and local ID. Reusing an ID in another Console does not create a shared Controller.

For example, this fragment gives two engines one throttle:

```json
{
  "control_layout": {
    "schema": 1,
    "controllers": [
      {
        "id": "propulsion",
        "console": "helm",
        "fallback": "hold",
        "commands": [
          {
            "id": "speed",
            "action": "speed_mm_s",
            "initial": 0,
            "consumers": ["port", "starboard"]
          }
        ]
      }
    ],
    "profiles": [
      {"id": "direct", "consoles": {"helm": {"human": ["propulsion"]}}},
      {"id": "automatic", "consoles": {"helm": {"ai": ["propulsion"]}}}
    ],
    "default_profile": "direct"
  }
}
```

`helm`, `port` and `starboard` must resolve in the enclosing entity. This is a layout fragment, not a complete pack. Each profile allocates every Controller exactly once. A Console selects its profile independently; human allocations take effect only while an operator is present and available. Unclaimed/Away Consoles use AI for every Controller. `fallback: hold` deliberately preserves accepted settings without making decisions; replace it with `tree` to author autonomous decisions.

To author two throttles, declare two Controllers or two owned channels and bind each engine to one channel. The compiler rejects two channels supplying the same physical input, including aliases. Ordinary numeric settings remain accepted until changed. `impulse` is a one-shot request and currently requires one executor. Request IDs identify retries; replaying an accepted ID does not repeat its effect. Operator generations reject writes queued before a handover. Held thrust, steering and boost are neutralised when authority changes.

Players select a Console's complexity profile in the lobby or its settings. A lobby change regenerates the prepared world and resets readiness; reselecting the current profile is a no-op. A player can configure their selected Console, and a ship host can configure its unoccupied or local Consoles. Another ship's or another player's Console cannot be configured through that route. In-game settings use the ordinary authenticated Console command path. Recovery retains the saved live profile and offers changes through settings after launch.

Throttle and heading widgets can name `controller` and `channel` instead of `system`. Both identifiers are required and resolve within the Console using that layout. The command must be a speed or heading setting matching the widget kind. The old semantic `binding` string is optional for this form. For example, a throttle widget can contain `"controller": "propulsion", "channel": "speed"`; its usual ID, label and layout coordinates are still required. It reads the accepted setting and compiled range from the Console board, so one widget can command both engines without choosing an engine as its target. An AI-owned section remains readable but refuses human writes.

## Preferences shared with AI

An `intent_integer` or `intent_boolean` setting has `consumers: []`. It represents an accepted Console preference, not a physical actuator input. Author bounds and an initial value as for other settings. For example, a human-operated planning Controller can own `{"id":"power","action":"intent_integer","minimum":0,"maximum":1000,"initial":650,"consumers":[]}`. An AI-operated generator Controller reads it through a same-Console `command_reads` binding and writes its own physical generator-output channel. Both operators use the ordinary command admission, generation, receipt and continuation contracts.

Preference settings persist across handover. A hold fallback retains the last preference when nobody operates its Controller. Reading a preference never grants another Controller permission to overwrite it. Physical actions still require compatible consumers; preference actions cannot bind equipment. Generated controls expose these settings and Workshop uses the same action vocabulary.

## Navigation commands

`destination` and `follow_contact` are boolean settings with an atomic `arguments` tuple. A destination channel supplies `[x, y]` in millimetres; a follow channel supplies `[contact, separation, speed_limit]` in entity ID, millimetres and millimetres per second. Coordinates are bounded to �1,000,000,000; separation and speed must be positive and at most 1,000,000,000. A disabled follow setting may use contact zero; enabling requires a positive contact and cannot target the owning entity.

For example, `{"id":"navigation","action":"destination","initial":0,"arguments":[100000,50000],"consumers":["port","starboard","rudder"]}` declares one owned destination setting. Consumers must be compatible engines or steering mechanisms. Both the enabled value and the complete tuple are admitted, committed and saved together. Scalar commands refuse extra arguments. Generated accessible controls edit these same tuples; follow target choices come from permitted Console contacts.

A Controller tree binds the channel with `{"channel":"navigation","type":"boolean","action":"destination"}` and writes `write.navigation` with `[true,100000,50000]`. For follow, declare `action: follow_contact` and write `[true,contact,separation,speed_limit]`. The accepted enable value remains `command.navigation`; tuple fields are `command.navigation.x` and `.y`, or `.contact`, `.distance` and `.speed`. These reads use the same frozen command snapshot as scalar channels. Timed scalar policies cannot implicitly provide a navigation tuple.

Guidance runs in the physical execution path using the accepted setting and permitted contacts, independently of who wrote it. Heading and manual navigation controls cancel active navigation; unrelated enabled/boost controls do not. Speed changes cancel follow but preserve destination guidance, allowing a separate cruise-speed setting. Destination guidance preserves the existing steer-toward-point behavior; it does not add route planning or obstacle avoidance. Follow retains the existing powered pursuit and stale-contact behavior.

## Controller trees and observations

Use `context: controller`. Declare `command_bindings` for the channels the tree owns; each supplies `command.<channel>`, an internal intent and `write.<channel>`. Declare `type: boolean` for boolean channels and `semantics: request` for request channels. Default types and semantics are integer and setting.

```json
{
  "id": "example.cruise",
  "context": "controller",
  "initial": "run",
  "command_bindings": [{"channel": "speed"}],
  "states": [
    {"id": "run", "running": [{"action": "write.speed", "arguments": [5000]}]}
  ]
}
```

This writes the same accepted throttle setting that a human control writes. It does not mutate an engine component. Physical consumption applies that channel to its compiled consumers. Consumers retain their own physical/resource constraints.

Available decision inputs include own position, velocity and heading; permitted record bindings; typed `system_bindings`; same-Console `command_reads`; and the addressed message inbox. A System binding names `name`, `system` and `field`, with an optional checked `type`. Availability comes from the fitted producer, so an engine without a resource store cannot supply `heat`. The System must also be in the Console's feed roster. A command read names `name`, `controller`, `channel` and `type`; reading another Controller's channel grants no write authority.

Each record binding also exposes `.available`, `.observed_tick`, `.received_tick`, `.source`, `.uncertainty`, `.kind`, `.withdrawn`, `.stale` `.revision` and `.subject`. Metadata describes the same permitted record selected for its value. When no live value remains, the latest matching stream's metadata explains its absence; withdrawal prevents falling back to an older value from that stream. With no matching record, `.available` is false and other metadata is absent.

Controller record bindings may use `subject: latest` to select the most recently published subject of that schema within the permitted board. All fields then read that subject's streams; a withdrawn latest subject does not silently switch back to an older objective. This supports scenario objectives whose subject is an objective identity, without privileged scenario reads.

`contact_bindings` select a same-Console Controller's `selected_contact` or `follow_contact` channel, for example `{"name":"tracked","controller":"navigation","channel":"follow"}`. They expose `tracked.available`, `.id`, `.x`, `.y`, `.vx`, `.vy`, `.heading`, `.speed`, `.observed_tick` and `.uncertainty`. Selection uses the frozen accepted command and reads only the Console's frozen contact feed. An undetected target has `available: false` and absent remaining fields. Current raw detection has zero uncertainty; this is not a sensor-estimation or tracking implementation. Human observations carry the same sample tick through local and remote projection.

Owned command bindings expose `.accepted`, `.revision`, `.consumed`, `.request_id` and `.pending`, alongside their values and any navigation tuple. `pending` distinguishes a newly committed command from its physical execution receipt; `accepted` describes the last consumption result. Human Console projections carry the same receipt fields. These are shared operational facts, separate from private tree continuation.

Decision inputs freeze before Controller evaluation. An earlier Controller's write becomes visible to other Controllers on the next tick. Private tree memory and continuation remain separate from Console observations and are never supplied to the GUI. Human and AI projections use the same operational observation producer.

## Reports and Coordination

Physical Systems can declare `reporters`, independently of AI trees:

```json
{
  "reporters": [
    {
      "id": "temperature",
      "field": "heat",
      "above": 500,
      "message": "example.hot",
      "recovery_message": "example.cooled",
      "debounce_ticks": 20,
      "recovery_ticks": 40,
      "minimum_gap_ticks": 100
    }
  ]
}
```

The enclosing fitting must actually produce `heat`. Both message templates must be permitted by the entity and physical observation owner. Reporter messages are Facts. Their dynamic bindings may use measured `amount`, threshold `flag` and System-name `detail` arguments, not AI memory or decisions. Healthy startup is silent; initial warnings obey debounce. Warning/recovery latches and reporting intervals survive handover and restoration.

A Controller destination is `{"kind":"controller","console":"helm","id":"propulsion"}`. It names the receiving Controller, not an engine. At delivery the current operator determines human notification or AI handling. Trees declare permitted `message_templates`, send through `send.<template>` with amount/flag/detail arguments, read typed `message_bindings` plus `message.*` metadata, and acknowledge an available message through `acknowledge_message`. Human message controls use the same authorised template route.

Entity-wide messages use destination kind `ship` in the current serialized vocabulary. Important AI-to-AI messages (urgency at least 2) also publish if their recipient remains AI at delivery. Publication requires a publishable schema and fields; dynamic public presentation parameters must bind a public payload value. Public copies retain identity and evidence but give spectators no action or acknowledgement authority. Ordinary targeted messages stay private.

## Compatibility and verification

Workshop's entity Properties inspector includes **Console Controllers and complexity**. Create an explicit layout or override the complete inherited layout, then edit Controllers, their commands and physical consumers, and each profile's Console allocations. Explicit layouts hide the old per-System policy and station-preset editors. Imported legacy policies must be removed before an explicit layout verifies.

Controller trees expose command, System observation, peer-command and message bindings. Their task palette comes from the production registry for the resolved tree, including inherited bindings. Inherited binding groups remain read-only until explicitly overridden; removing a local group restores inheritance. System Properties expose factual reporters, and Coordination destinations include Console/Controller addresses.

Isolated Test feedback identifies each Controller by entity, Console and Controller ID, with its effective operator, generation, active states, transition reason, diagnostics and bounded command receipts. This debugging view is separate from player observations and does not export private tree memory.

Explicit layouts use World save schema 5 and Controller knowledge binding metadata. Saves without a defined continuation/ownership mapping are refused transactionally. The command transport uses wire envelope schema 4 and carries Controller, channel, operator generation and request identity, plus bounded command arguments. Flight continuation schema 4 includes destination guidance; the loader still understands schemas 2�3 with navigation inactive. These version checks do not imply that the remaining legacy runtime has been removed.

Use the production Catalog loader to validate bindings, then synthetic integration fixtures to verify behavior. `tests/support/controller_fixture.hpp` and `tests/system/controller_runtime.cpp` exercise the actual load, authority, command, physical, messaging and restore paths. Shipped ship tuning is not a test golden. The refactor plan remains the source for unimplemented migration and acceptance work.
