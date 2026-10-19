# Gameplay implementation phase

Approved scope: the full gameplay and authoring plan agreed on 19 October 2026.
This record tracks implementation, independent review and verification separately.
An example mission is evidence, not the scope boundary.

## Delivery ledger

| Stage | Required behavior                                                                                                                                                           | Status      |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| 1     | Fitted System instances, reusable equipment/assemblies, exclusive ownership, typed bindings, compatible commands/projections/saves                                          | Implemented |
| 2     | Console and shipwide blackboards, typed provenance records, scoped feeds, explicit publication, private memory boundaries                                                   | Implemented |
| 3     | Hierarchical System/scenario trees, ordered tasks/transitions, inheritance, references, interruption, visual Workshop editing and disposable testing                        | Implemented |
| 4     | Generation, local reserves, finite conduits/routing/isolation, group distribution AI, random overload cuts/restoration, thermal protection/overrides, coolant and forecasts | Implemented |
| 5     | Reverse/manual/lateral flight, boost, V2 impulse, Match Speed, swept collisions, paid actuation and graphical/semantic controls                                             | Implemented |
| 6     | Scenario instances/events, dynamic entity/group spawning, objective lifecycle, reach-region crossing, branching and authoring                                               | Implemented |
| 7     | Typed Coordination, addressed/broadcast delivery, human/AI presentation, expiry/replacement/withdrawal, history and controller policies                                     | Implemented |
| 8     | Generic station presets, summary intent, live delegation/Away/backfill, human-seeking routing and safe handover                                                             | Implemented |
| 9     | Reusable base/training content, equipment/roster variants, remote play, recovery/checkpoint continuation and acceptance evidence                                            | In progress |

Every stage includes applicable production loading/verification, Workshop authoring,
player semantics/localisation, native/browser integration and persistence. No stage
is complete merely because a headless mechanism or a demonstration works.

Implemented denotes delivered software behavior. Physical-device, audible-reader,
six-display performance and tuning acceptance are recorded separately below.

## Contracts retained throughout

- Individual System controllers share their Console's permitted knowledge and
  explicitly published shipwide data; private AI memory stays independent.
- Shipwide knowledge initially contains objectives and deliberate publications.
  Rendering visibility does not automatically publish observations.
- One effective control source per System; all human/AI commands use admission.
- Generator capacitor and consumer batteries share one storage model. Actual
  resources pay actual output; unused reserved flow is not redistributed silently.
- Seeded random incoming-flow cuts and sustainable random restoration preserve
  requested settings and local reserves. No hidden critical-group exemptions.
- Cooling reservations consume capacity; only actual removal consumes coolant.
- Ending boost/impulse preserves momentum. Match Speed uses observed motion only.
- Spawn/objective/message effects survive replay and recovery without duplication.
- Explicit Away, not inactivity detection. Presets, not numerical complexity scores.
- Fixed forward Viewscreen and existing native/browser entry paths remain in use.
- Synthetic fixtures stay independent of shipped content and tuning. Physical
  device and audible reader evidence remains separately recorded.

## Agreed deferrals

Conduit damage/repair/bypass construction; coolant recycling/external resupply;
automatic visual awareness; inter-ship Comms/relay simulation; combat and general
damage/repair/logistics; navigator passage and formation navigation; dynamic server
membership and procedural mission planning.

## Verification and review receipts

### Stage 1a: independent fitted identities

Implemented exact System identity through definition verification, Console
ownership/read feeds, command admission/queueing, per-instance controller and
actuator state, player widget bindings, restricted crew assets and schema-3
continuation. Legacy capability-keyed worlds retain schema-1/2 save behavior.
The packet envelope is schema 2 so incompatible peers are refused.

Independent review identified legacy-key fallbacks, incomplete visual bindings,
malformed-save acceptance and portable session schema handling. These were fixed
before commit. The reviewer found no remaining blocking source findings for 1a.

Verification: all 12 native integration cases, headless WASM cases and matching
native/WASM continuation passed. The real network gate passed seven cases and
exposed a new schema-3 portable-reader omission in the named-instance fleet case;
after fixing it, native/WASM portable-session regressions and the real three-host
fleet/checkpoint/crew-command journey passed. The latter uses independently owned
`drive`/`reserve` engines and renamed radar through remote projections and commands.
No physical-device or graphical smoke evidence is claimed by these checks.

At the end of this step, reusable equipment/assembly composition and broader
Workshop authoring remained subsequent work. Later receipts supersede that status.

## Reusable gameplay foundations (stages 1b, 2a, 3a, 4a and 6a)

Implemented production compilers for reusable equipment and typed assembly service
bindings, scoped records and Coordination templates, hierarchical contextual trees,
resource networks, flight definitions and scenario definitions. Authoritative domain
execution is a separate library; player and Workshop builds link definition compilers
without acquiring simulation execution. Assembly expansion is bounded and editable
composition survives Workshop verification/export. Template verification runs even
for unused library definitions.

Independent review identified and resolved inherited named-state patch loss,
composition edit loss, cyclic self-supply, fractional-rate reservation cuts, missing
unused-template requirements, replay ancestry collisions and scenario partial-effect
admission. Native and WebAssembly production-loader system journeys pass for composed
equipment, conservation/routing/cooling/protection, scoped knowledge and delayed
Coordination, hierarchical/referenced trees and continuation, and scenario spawns,
region crossing, objective/publication credit and resumed independent situations.

This commit verified the foundations. Full World actuation, crew projections and
controls, station handover, graphical authoring and disposable-host integration
were verified in the subsequent integrated step below.

## Integrated gameplay and authoring

The authoritative World now executes the compiled mechanisms through the ordinary
System ownership and command paths. Finite shared reserves, generators, redundant
conduits, incoming group budgets/cuts/restoration, powered coolant, thermal stops,
overrides and overdrive fund actual flight and sensor effects. Fractional work is
prepaid and retained as bounded credit; pausing, sharing a reserve and recovery
cannot create unpaid output. Forecasts state their constant-current-operation
assumption. Reverse, manual axes, lateral thrust, assist, boost, charged impulse,
observed-only Match Speed and swept shape collisions use the production tick.

Console records and selected ship publications retain scope, provenance, age and
reader permissions. System/scenario trees use hierarchical tasks, inherited states,
references and independent continuation. Structured messages evaluate their actual
typed observation/intent/memory/parameter bindings at emission time. Delayed routing
uses current human/AI control, named request keys, expiry/withdrawal/replacement and
independent AI cursors. Explicit human-seeking offers reach eligible present humans
without transferring actuator authority or leaking offers into other AI inboxes.

Simple/Middle/Detailed presets delegate whole Systems to the same ordinary vacancy,
Away and disconnect controllers. Named summary-channel bindings let one controller
consume several independent intents. Claims execute AI exit cleanup before human
authority; released human axes become neutral while already-AI operation remains
intact. Schema-4 continuation validates physical/knowledge rosters, instances,
allocators and authority. Remote clients use exact projected controls and bounded
whole-entry knowledge snapshots with visible omission counts.

The Workshop edits equipment, assemblies/services, resources, knowledge, messages,
ownership/presets, System/scenario trees and reusable situations through the same
compilers. It preserves authored composition and inherited named-state edits,
repairs canonical scenario references on rename, and exports verified dependencies.
Native and browser Test use isolated production Worlds with start/pause/step/restart/
stop, active controller states, scoped records, pending messages/commands and actual
resource grants. The browser host is a separate headless Worker; Workshop and crew
clients retain their simulation dependency boundary.

Independent stage review identified and resolved authority-transition bypasses,
AI input loss during release, disabled-radar recovery, ignored message bindings,
multi-Console Station routing, summary-channel aliasing, unchecked action arithmetic,
unbounded context capture/projection, fractional payment, missing composition
preview trees and Worker upload-stack/error-feedback faults. Scenario and Workshop
changes received additional independent review. No blocking source finding remains.

Verification: `scripts/check.ps1` passes all 20 native system cases, the existing
WASM checks and eight new gameplay WASM journeys, matching native/WASM continuation
traces, production pack verification and all eight real network cases. The remote
journey operates powered named Systems, summary/preset/delegation/Away controls,
scoped objectives and finite reserves, then crosses the automatic checkpoint and
recovers returning peers and a portable session. Large accepted knowledge histories
cross the authenticated chunked projection path with explicit whole-entry bounds.

Reviewed followups reject future-dated saved observations and preserve authored
summary labels in player controls. All 20 native cases pass after those changes;
the affected flight and summary WASM cases are checked again after relinking.

Actual browser Worker lifecycle and graphical Workshop edit/verify/export/test
evidence is recorded in [the manual smoke receipt](smoke/gameplay-worker.md).
The Cruiser composition preview resolves, an edited equipment draft verifies
through the production loader, and its exported snapshot runs and disposes in the
isolated browser host with actual resource diagnostics. These checks do not claim
every inspector path, physical touch/gamepad routing, audible reader interaction,
six-screen performance or production tuning has been playtested. Those manual
acceptance checks remain separate; no additional software subsystem is deferred
from this approved gameplay phase.
