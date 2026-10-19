# Gameplay implementation phase

Approved scope: the full gameplay and authoring plan agreed on 19 October 2026.
This record tracks implementation, independent review and verification separately.
An example mission is evidence, not the scope boundary.

## Delivery ledger

| Stage | Required behavior                                                                                                                                                           | Status      |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| 1     | Fitted System instances, reusable equipment/assemblies, exclusive ownership, typed bindings, compatible commands/projections/saves                                          | In progress |
| 2     | Console and shipwide blackboards, typed provenance records, scoped feeds, explicit publication, private memory boundaries                                                   | Pending     |
| 3     | Hierarchical System/scenario trees, ordered tasks/transitions, inheritance, references, interruption, visual Workshop editing and disposable testing                        | Pending     |
| 4     | Generation, local reserves, finite conduits/routing/isolation, group distribution AI, random overload cuts/restoration, thermal protection/overrides, coolant and forecasts | Pending     |
| 5     | Reverse/manual/lateral flight, boost, V2 impulse, Match Speed, swept collisions, paid actuation and graphical/semantic controls                                             | Pending     |
| 6     | Scenario instances/events, dynamic entity/group spawning, objective lifecycle, reach-region crossing, branching and authoring                                               | Pending     |
| 7     | Typed Coordination, addressed/broadcast delivery, human/AI presentation, expiry/replacement/withdrawal, history and controller policies                                     | Pending     |
| 8     | Generic station presets, summary intent, live delegation/Away/backfill, human-seeking routing and safe handover                                                             | Pending     |
| 9     | Reusable base/training content, equipment/roster variants, remote play, recovery/checkpoint continuation and acceptance evidence                                            | Pending     |

Every stage includes applicable production loading/verification, Workshop authoring,
player semantics/localisation, native/browser integration and persistence. No stage
is complete merely because a headless mechanism or a demonstration works.

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

Stage 1 remains in progress: reusable equipment/assembly composition and broader
Workshop authoring are subsequent logical steps. Later stages remain pending.
