# Powered ships and authored scenario content

The base pack now authors reusable equipment and operating policies for the full power/Helm/knowledge phase. The optional Training pack composes those definitions into scenario journeys. These definitions are content-verification inputs; their current numerical values are tuning, not performance goldens or claims of completed device acceptance.

## Base-game library and ship fits

| Definition                      | Purpose                                                                                                                                         |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `base.equipment.*`              | Engine, steering, lateral thrust, boost, impulse, radar, generator, storage, distribution, coolant valves and a powered coolant pump            |
| `base.assembly.power-core`      | One real generator/capacitor, service reserve, finite pump feed, coolant stock/provider, thermal controls and exposed typed supply endpoints    |
| `base.assembly.drive`           | Independent propulsion mechanisms and local batteries, coarse/fine incoming budgets, parallel engine feeds and explicit receiving coolant links |
| `base.assembly.sensors`         | Local powered radar with its own reserve, supply budget and cooling allocation                                                                  |
| `base.assembly.console-service` | Powered Console service reserve with independently controlled incoming service and cooling                                                      |
| `base.courier-engineering`      | Split Helm/Engineering fit using one power core, one drive assembly and sensors                                                                 |
| `base.cruiser`                  | Six independent Consoles, two drive placements, separate Science/Tactical sensors and separate Comms/Captain service reserves                   |

The established Courier (`base.courier`) and Free Flight IDs remain available. Free Flight additionally permits the new fits. Existing Training navigation/lighting content remains independent of the new exercises. New fit construction uses inheritance and assembly placements; it does not duplicate a provider for each consumer. The Cruiser uses the existing ship art at a larger authored scale while supplying its own physical dimensions and equipment configuration.

Helm owns its actual propulsion/steering mechanisms. Engineering owns generator, main reserve, distribution and cooling services. Science and Tactical own their separate sensors. Comms and Captain operate their current service reserves and typed Coordination/shared-information controls; this phase does not claim that later dialogue, weapons, repair or other specialist gameplay is implemented merely because those Consoles exist.

All local reserves have real finite quantities, transfer limits and thermal parameters. Boost and charged impulse draw their authored operating costs. The central coolant provider names the pump's `power_store`, so unpaid pump operation cannot remove heat or spend coolant. Redundant engine feeds share the same upstream stock, and individually isolated links do not create replacement capacity. A second drive placement receives independent local state and controller instances.

## Console Controllers, complexity and information

Maintained craft author Console Controllers with owned command sections and explicit physical subscriptions. The survey, tug and traffic retain vector mechanisms; powered engineering and patrol fits retain inertial mechanisms. AI trees operate these command sections through the same admission and execution as human controls, reading permitted Console observations and entity knowledge. Physical fittings retain resource and thermal behavior; always-active factual reporters handle heat warnings and recovery independently of the operator.

Engineering Simple assigns routine actuator Controllers to AI and exposes coarse preferences through a human Controller. Middle exposes generator and coarse distribution commands while retaining routine fine service assistance. Detailed exposes the supported service commands. Each Console selects an authored complexity profile, with no individual System delegation toggles. Vacancy, Away and disconnect make every Controller on that Console AI; returning humans regain the selected allocation. Fully AI entities use this same path.

Every new Console explicitly declares its reader and machinery-producer roles. Machinery status records bind permitted `stored_energy`, `heat` and `supply_limited` readings. Stored energy and temperature remain private fields. The named `propulsion-availability` publication shares only the permitted limitation field when deliberately activated. A separate authored Helm-to-Engineering feed carries that selected field to its declared recipient. Neither rule aggregates other Console boards or turns renderer visibility into knowledge.

The base Coordination schema has typed amount/flag/detail fields. Templates cover power/cooling requests, supply limitation/recovery, thermal protection and navigation advisories. Messages address Controllers by Console and Controller name, or the whole entity. Important AI-to-AI deliveries also appear on the public board; ordinary addressed messages remain private. The human Engineering request template targets the named Engineering Console and explicitly offers work to eligible present humans when needed. Offers preserve the addressed AI route and never transfer actuator authority. Semantic payloads and presentation text remain separate; delivery and authority follow the normal Coordination adapter.

Authored player text uses the existing source-text localisation keys. Their English entries live in `content/locales.json`, and the ordinary translation/pseudo-localisation path applies. New content does not introduce a separate language loader.

## Training scenarios and reusable situations

`test.powered-navigation` starts from a published destination marker and progresses to a longer transit region. `test.powered-navigation-offset` changes the actual region geometry and destination direction while reusing the same scenario tree and ship definitions.

The shared scenario tree:

1. Requests departure traffic through normal scenario spawn construction.
2. Adds the first reach-region objective, publishes it to the assigned ship and waits for executed centre entry/crossing.
3. Requests follow-up traffic and adds the second objective.
4. Starts another independent observer situation, then waits for the second physical completion.
5. Emits a declared completion event and ends successfully. Required objective-admission failure takes an explicit failure branch; unavailable optional traffic reports its condition and retains the navigation journey.

The scenario selects the latest objective belonging to its own situation, rather than hard-coding a single objective handle into every stage. Receipt-gated transitions wait for the committed result. Re-evaluation or recovery cannot repeat an accepted spawn/objective action. The two observer configurations reuse `test.route-observer` with distinct parameters and independent instances; each responds to the declared completion event without accessing the other's memory.

Circular and rectangular regions use authored millimetres. Starting inside, entering or crossing at high speed completes the assigned objective without a stop/dwell requirement. Publishing a destination does not take human Helm control. Delegated Helm may pursue the approved destination through its normal admitted steering/propulsion behavior.

Scenario advisories use the explicitly declared `test.scenario-advisory` knowledge schema, with the trusted scenario producer and permitted crew readers. No hidden future spawn schedule, scenario-private state or unobserved ship internals are included in ordinary player records.

## Verification and playable evidence

The production pack resolver must verify the base pack alone and then the dependent Training pack, including unused reusable definitions, service types, System ownership, resource links, tree contexts, presets, message permissions and objective/spawn references. The authored corpus intentionally exercises repeated same-kind Systems, inherited equipment, independent Consoles and a region variant. None of those checks establishes current tuning quality.

Manually triggered playable journeys should cover both new fits, two-human Helm/Engineering, either seat backfilled, six Console routes, Simple/Middle/Detailed changes, Away/return, a constrained supply/cooling request, boost/impulse, scenario progression and save/recovery. Workshop should be used to vary an inherited equipment parameter, an assembly override and a region, then verify/export/reload through production paths. Native/browser, screen-reader and physical-display evidence remains separately recorded in the implementation ledger; this document does not report those runs as complete.
