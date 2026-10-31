# Scored ship objectives

Implementation contract · 31 October 2026. This supersedes single-objective Captain selection and native decision loops. Delivery evidence is recorded separately below; this contract is not a completion claim.

Each Controller evaluates only its immutable permitted Console observations, shipwide objectives, addressed messages, accepted command bindings and private continuation. Scenarios publish circumstances and objectives, never machinery commands. Physical admission remains authoritative.

Selection has three passes: feasible Reaction work; feasible Captain-marked work; remaining feasible work. Within a pass, compare tier, then integer utility, then stable objective/target/action identity. Tiers descend through Reaction, Scripted (High), Objective (Combat), Directive (Combat), Scripted (Low), Objective (Other), Directive (Other). Captain can mark multiple objectives; marks change the pass, never the tier. A commitment bonus applies within a tier only.

Authored scoring combines urgency, expected progress, observed risk, travel and resource costs, and commitment. Missing evidence is not physical truth. Infeasible work does not block lower priorities or change global objective status. A new valid support request advances work; waiting for its reply does not monopolise a Controller. Higher-ranked work uses ordinary action cancellation without undoing physical progress.

Important information travels in scoped reports with matching readable prose and typed payload, provenance, revision, uncertainty and validity. Requests, acceptance and completion remain distinct. Delivered private reports affect their recipients, not every Console. New reports and physical outcomes become decision inputs on the next tick.

Teams know all physical facts in their current compartment through local observations. Controllers choose destinations from known work; local workflows select repairs and patients after arrival. Idle automatically assigned teams return to their authored home (default: fitted starting compartment), without searching hidden remote state. Explicit human orders retain authority.

## Native selector migration inventory

| Selector                                                         | Classification and required boundary                                                     |
| ---------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Interdiction movement, targeting, generator and departure orders | Policy: replace with published objectives executed by ordinary Controllers               |
| Automatic repair destination                                     | Policy: choose reported work or home; no remote physical fault search                    |
| Repair task at current location                                  | Local workflow: complete local observation; physical work retains finite payment         |
| Medical destination/order and facility selection                 | Policy: known assignments and facility reports; local triage remains a workflow          |
| Away field-care destination                                      | Policy: reported destination; complete information only at current location              |
| Bay defend/bomb/recall                                           | Policy: frozen craft reports and explicitly bound target intent                          |
| Away contribution, staffing, transport, escort and send          | Department Controller policy with admitted requests/receipts                             |
| Scanner objective target                                         | Permitted calculation over published objectives/requests, not finding definitions/events |
| Point-defence candidate ranking                                  | Permitted calculation over observed trajectories, never actual projectile intent         |
| Craft readiness and guidance inputs                              | Scoped factual producer, frozen before policy evaluation                                 |
| Navigation assessment                                            | Approved shipwide factual producer; does not issue commands                              |
| Admitted docking assistance                                      | Physical execution of an accepted operation                                              |
| Resource allocation, routing, treatment and finite work payment  | Physical execution; may refuse work but cannot invent strategic assignments              |

## Delivery

Pending. Verify each logical migration through synthetic production-loader integrations, preserve continuation deterministically, and record native/WASM/network and manually invoked smoke evidence independently. Do not infer completion from this document or from compilation.

### Foundation evidence — 31 October 2026

The native Controller integration passes the seven-tier comparisons, multiple/idempotent Captain marks, private-objective refusal, AI Captain revision admission, frozen same-tick marks and save/resume. Native combat, FTL, away, psychic and craft integrations also pass. Production base packaging and verification accepted the explicit Rescue scan objective (`build/scored-objectives-base.zip`). These checks used the working tree, including the existing unrelated demo/build changes.

This first increment implements the scoring primitive and navigation/scan bindings, multiple Captain marks and their projections/persistence, frozen bridge intent, scoped craft/FTL decision inputs, and trajectory-only missile ranking. It does not establish objective arbitration for every Controller behavior. Standing directives, native team/Medical assignment, bay dispatch, away preparation and interdiction command ownership still require migration. No WebAssembly/network gate, graphical smoke, hardware or screen-reader acceptance is claimed for this increment.

### Repair evidence — 31 October 2026

Automatic repair destinations now come from frozen known damage, owned-system malfunction reports or accepted repair requests. Teams inspect all local faults on arrival, perform only local work and return home when known work is exhausted. Explicit human destinations remain authoritative. The shared Workshop field exposes an authored home, defaulting to the fitted starting compartment.

Synthetic combat integrations exercise hidden remote faults, delivered requests, inspection before completion, home return, unknown-home refusal and checkpoint continuation. Native combat, Controller, Medical and away integrations pass; the Workshop gameplay editor builds. The officer-travel fixture now explicitly assigns both teams to its known work rather than relying on the removed hidden search. Medical and away destination selection still require their own migration.

### Medical local-information evidence — 31 October 2026

Shipboard destination recommendations now use immutable team observations and explicitly scoped departmental roster reports. Each new assignment runs through the shared Controller executor and ordinary team admission. Human assignment authority blocks automatic replacement. Local patient discovery, useful treatment and home return pass synthetic integrations, including two worlds whose undisclosed remote patient severity differs. The scoped patient rows retain source, observation tick and revision.

Deployed care no longer searches target compartments. `away.care` admits an explicit destination for the assigned Medical team's owner, with no transfer of authority to Dispatch. Its scoped local report, player controls and destination/route/work continuation are preserved through projection and save/restore. Recovery facility selection uses frozen capacity and route reports before physical capacity checks. Native Medical, away and Controller integrations pass, and the player operations widgets build. Two older fixtures now explicitly assign their known patient destinations, preserving their clinical effectiveness and travel/continuation assertions.

This increment does not yet bind Medical work to published standing objectives or introduce a remote field-patient report/request protocol. Those remain part of the shared arbitration and evidence work; these passing local-information checks do not establish overall migration acceptance.

Production packaging and independent verification pass for `build/scored-objectives-medical-base.zip` (hash `bafda25d8bd7a41b7dfdbe89f146b35d15876bc4d22bd5122312f92efc0ea108`), including both the Medical assignment and readiness execution trees. This is working-tree evidence and includes the pre-existing unrelated demo/build changes.

### Shared objective/action bindings — 31 October 2026

Compiled bindings now connect published behaviors to prepared shared Controller actions. The common evaluator checks publication lifecycle, applicability, action availability, delegated write capabilities and named scoring support before applying the three passes and tier-first integer utility. Stable binding/target/action arguments supply deterministic ties and exact-input commitments. Selection and preemption use ordinary shared action admission/cancellation; commitment identity and action continuation survive checkpoints. Diagnostics and model snapshots expose eligibility reasons and score terms.

Ships explicitly fit standing directives through the same shipwide record interface, with protected producer authority, provenance and reserved identities. Captain can mark those records. Scenario allocation remains below the reserved directive range, and departure reports exclude directives from mission results. Medical work/home policies now use these publications and bindings; other departmental and encounter selectors still require migration. Workshop exposes objective metadata, behavior/action bindings and scoring weights.

Native Controller, Medical, FTL and scenario integrations pass. The Controller cases cover tier-before-utility selection, Reaction precedence, multiple marks, same-tier commitment, stable ties, unsupported-policy fallthrough, private-record isolation and checkpoint continuation through actual action execution. Medical's hidden-patient/local-discovery/home-return fixture uses the shared objective path. The FTL case confirms that fitted standing directives do not enter mission grading. The Workshop gameplay target builds. These checks do not establish the remaining behavior migrations, execution-refusal retry handling, WebAssembly/network gates or manual smoke acceptance.

Production packaging and independent verification pass for `build/scored-objectives-bindings-base.zip` (hash `14f43e93d0f1e0b5d3b779704693255f5939702513e1cb8cdd0716780b442f86`). Production verification also rejects duplicate candidate identities, nested direct selectors and reserved objective-producer grants to Consoles before session creation. The archive includes the pre-existing unrelated demo/build work.

### Refused execution and retry evidence — 31 October 2026

A failed scored execution now creates a bounded private receipt for its action, target and exact typed inputs. It temporarily removes that execution from feasibility while other objectives can advance. Authored retry expiry allocates a new ordinary action attempt. Checkpoints preserve the delay and counter; operator-generation changes and capture invalidate old receipts. Existing action cancellation and physical progress semantics remain in force. The retry delay is authored in the production bindings and exposed in Workshop.

Synthetic Controller integration forces a real command-admission refusal on higher-tier work, confirms that lower work proceeds, observes a later distinct attempt, checks paired save/restore, and verifies invalidation on human handover. Native Controller, Medical and away/capture integrations pass; Workshop gameplay builds. Independent production verification accepts `build/scored-objectives-retries-base.zip` (hash `7f7db3ed45143d0f3e28eaafc1ba31aa0991c48b03fdb8f37a2414d17e8bf5af`). This working-tree archive includes the existing unrelated demo/build work. Remaining departmental/encounter migrations and final cross-platform/network/smoke gates remain pending.

### Enumerated Medical candidates — 31 October 2026

The scored path now evaluates each known Medical destination and accepted continuation separately. Frozen observations supply route reachability/travel, availability, escorts and facility capacity. Authored bindings supply severity utility, retasking cost and continuation commitment. New assignments use ordinary admission; continuation leaves the accepted physical workflow running. Objective publications, projections and Workshop expose operation, compartment, team/system and mission parameters. Shipboard Medical rejects requests outside its supported scope.

Synthetic Medical integration confirms that an unreachable higher-tier reported severe patient does not suppress reachable minor-patient care. It closes an authored route through normal compartment-access admission and checks candidate diagnostics and useful treatment. Existing hidden-patient, local-discovery, home-return, human-authority and checkpoint cases pass. Native Controller, scenario, away and disposable Workshop integrations pass. Independent production verification accepts `build/scored-objectives-medical-candidates-base.zip` (hash `4b20e35260f317e54384f489e8d3716a1a8ece1c6668861e30cacb2038b73d80`). This working-tree archive includes the existing unrelated demo/build changes. Other departmental selectors, reports and final cross-platform/network/smoke gates remain pending.

### Controller-owned interception — 31 October 2026

The native weapon loop no longer selects a point-defence target. A pure calculation ranks frozen observed missile trajectories by estimated closest approach, time to approach and stable track identity. Authored Reaction bindings on the weapon Controller submit the selected track through ordinary target admission and shared action execution. Standby and known hostile engagement remain separate candidates. Interceptor flight actions also submit their observed interception target. Machinery tracks, aims and fires at that accepted target, checking current evidence and physical constraints. It no longer tests an undisclosed projectile source faction to choose whether an interceptor craft should fire.

Weapon and directed target contracts now include scoped missile-track identities. The common native/browser controls and accessible contact selection expose these same tracks to human operators. Synthetic combat integrations cover a changed undisclosed missile target with identical Controller commands/diagnostics, autonomous interception, human target admission, physical tracking/firing, independent targeting mode and checkpoint continuation. Native combat, directed-weapon, craft and Controller integrations pass. Independent production verification accepts `build/scored-objectives-pdc-base.zip` (hash `2455259a5418a994482b01c617b88501576eb97fb11cae95b594460377f00e29`). This working-tree archive includes existing unrelated demo/build changes. Craft dispatch, away preparation, interdictions, remaining evidence protocols and final cross-platform/network/smoke gates remain pending.
