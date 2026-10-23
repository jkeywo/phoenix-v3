# Cruiser station complexity

Agreed implementation contract, 23 October 2026. This increment covers Helm, Tactical and Engineering. Science, Comms and Captain retain their previous workloads. The three revised stations default to one star independently, including in the lobby. Vacant or Away stations use ordinary AI control independently of their selected stars.

## Workloads

| Station     | One star                                                                                                                                                                                                                            | Two stars adds                                                                                                                         | Three stars adds                                                                                                                                     |
| ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Helm        | Assisted speed, heading, navigation/follow/match-speed, coordinated boost/impulse and ordinary docking. AI coordinates the second assembly and machinery.                                                                           | Direct longitudinal, lateral and steering inputs.                                                                                      | Individual assembly settings/impulse, machinery enablement, fitted overdrive/thermal overrides and Sailing Duty Officers.                            |
| Tactical    | Shared combat target; individual guns/banks enabled or disabled; general Dispatch. AI manages ammunition, readiness, defensive PDCs, combat craft, Security and officers.                                                           | Individual combat-craft orders/targets/recall; PDC enablement with automatic offensive/defensive selection.                            | Individual weapon targets and ammunition, full PDC settings, radar/bay/armament readiness, local stocks, Security assignments and Tactical officers. |
| Engineering | Normal/Combat/Recharge Power; individual repair-team compartment deployment; Engineering Dispatch and department contributions. AI handles detailed power/cooling, work within compartments, utility shuttles, stocks and officers. | Reactor output and major distribution allocations; system/hull repair priorities; individual utility-shuttle contributions and recall. | Reserve limits/overrides, detailed feeds/isolation/cooling, main stocks/material transfers/cargo settings, access routes and Engineering officers.   |

All levels retain ordinary execution automation. Engineering retains power and cooling authority over Helm machinery. Tactical retains its shared PDC command group and six four-tube missile-bank controls.

Repair priorities finish the selected system or compartment hull before resuming useful work in the compartment. Selecting another compartment clears the previous system/hull priority. Main logistics, repair kits and craft-ammunition supplies use a dedicated Engineering logistics Controller; repair-team access cannot change stock targets.

Shared flight settings follow the main assembly until an explicit three-star override is made. A downgrade returns the second assembly to shared commands. Coordinated impulse admits separate, idempotent requests for each physical executor. A request still has exactly one executor. Profile handover preserves physical work and clears unavailable held inputs and overrides.

## Dispatch and contributions

General `dispatch` belongs to Tactical. `engineering-dispatch` belongs to Engineering, together with the two-shuttle utility bay. Missions may name an explicit fitted Dispatch System with `dispatch`. Omission keeps the legacy `dispatch` System, or the unique fitted Dispatch when it has another ID.

The owning Dispatch opens, sends, recalls and cancels the mission. Each contributing watch authorizes its own personnel/craft contribution. Below three stars, officers are selected automatically within that department. One-star craft selection is automatic after authorization; two stars permits individual craft selection. Vacant watches can prepare requested contributions through their ordinary command authority.

Optional `craft_slots` declare exactly one required `transport` and up to eight `escort` slots. Each has `id`, `role`, optional `craft_role`, and `required`. Escort slots use `craft_role: "interceptor"`. Supply transports use `craft_role: "shuttle"`. Existing `transport` selection remains the primary transport and existing missions need no craft-slot migration. Missions with craft slots may have zero personnel slots.

An asset is reserved during preparation and cannot also serve another mission or cargo job. Assigned escorts launch first; the primary waits for their launch readiness. Mission recall/cancellation requests escort recovery. Individual escort recall withdraws that contribution without requesting passenger pickup. Escort loss is reported and the surviving transport continues; transport loss fails the operation and recalls surviving escorts. Recovery releases commitments through the existing physical paths.

## Supply offers

The Cruiser combat exercise includes a House depot with spare-parts gifts and medical-for-parts barter. Hail its quartermaster through Comms and accept an offer to disclose the corresponding Engineering mission. Disclosure does not launch a shuttle. An optional Quartermaster officer and Tactical interceptor escort can accompany collection.

An away mission may use `hail_tree` instead of a map `conversation` ID. The delivered disclosure line must belong to that conversation program, and the live thread must include the player's ship and the authored target. `reveal_event` remains the delivered line's text key.

The mission's `supply` object contains:

- `provider_store`, `home_store`, `material` and `limit`.
- Optional `payment`, `give` and `pay` for fixed material barter. A batch exchanges `pay` payment units for `give` supply units. Gifts omit payment. Payment and supplied material must differ.

One dispatch performs one trip. At one/two stars, the manifest uses authored stock deficits, actual committed arrivals, finite provider stock, payment, receiving space and shuttle capacity. Unfunded replenishment requests do not count as incoming goods. Three stars can select a valid quantity or restore automatic sizing.

Cargo jobs perform payment loading, outbound travel, ordinary docking, payment, collection, automatic return and home unloading. Stock is moved through ordinary located material batches. Provider goods, incoming payment space, cargo and home receiving capacity are reserved against competing work. Completion requires physical unloading aboard the Cruiser. No mission-completion action creates supplies.

Cancellation stops further requests, settles existing batches and returns remaining cargo physically. Actual payment/collection/delivery receipts survive interruption; incomplete exchanges report failure and do not invent refunds. Missions retain their craft commitments through recovery and unloading.

## Authoring and continuation

`control_layout.station_complexity` also accepts a list of Station IDs so grouped workloads can be enabled selectively. `default_profiles` supplies per-Console defaults. `manual_workloads` supplies minimum star levels for the specialized `craft`, `transport` and `logistics` command families. Human command admission, client eligibility and the interface use the same workload rules.

Setting channels may use `shared_command` with `controller` and `channel` to follow an independent same-Console setting of the same action. This shares the existing explicit-override/handover mechanism with weapon `shared_target`. Requests cannot use this setting relationship. Impulse channels can instead declare a `coordinated_group` and `individual_stars`; their group action issues one request to each executor.

Repair teams opt into compartment-local preferences with `local_repair_priority`. Their `repair_system` channel can declare `hull_priority` to expose the compartment-hull choice, and `individual_stars` gates detailed priorities independently of compartment deployment. Existing teams keep their previous assignment semantics unless they opt in.

World continuation is schema **20**, with mission continuation schema **11**. Older World saves require a new session. Current-format saves retain mission routes, department authorizations, escort assignments, supply manifests, cargo reservations and actual transfer receipts. Older authored single-Dispatch/single-transport missions remain supported.

Verification evidence will be recorded after the final native, WebAssembly and explicitly invoked console checks. Physical screen-reader, touch/gamepad and six-display acceptance require separate device runs.
