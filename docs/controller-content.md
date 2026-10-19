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

Explicit layouts use World save schema 5 and Controller knowledge binding metadata. Saves without a defined continuation/ownership mapping are refused transactionally. The command transport uses wire envelope schema 3 and carries Controller, channel, operator generation and request identity. These version checks do not imply that the remaining legacy runtime has been removed.

Use the production Catalog loader to validate bindings, then synthetic integration fixtures to verify behavior. `tests/support/controller_fixture.hpp` and `tests/system/controller_runtime.cpp` exercise the actual load, authority, command, physical, messaging and restore paths. Shipped ship tuning is not a test golden. The refactor plan remains the source for unimplemented migration and acceptance work.
