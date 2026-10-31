# Scored ship objectives

Implementation contract · 31 October 2026. This supersedes single-objective Captain selection and native decision loops. Delivery evidence is recorded separately below; this contract is not a completion claim.

Each Controller evaluates only its immutable permitted Console observations, shipwide objectives, addressed messages, accepted command bindings and private continuation. Scenarios publish circumstances and objectives, never machinery commands. Physical admission remains authoritative.

Selection has three passes: feasible Reaction work; feasible Captain-marked work; remaining feasible work. Within a pass, compare tier, then integer utility, then stable objective/target/action identity. Tiers descend through Reaction, Scripted (High), Objective (Combat), Directive (Combat), Scripted (Low), Objective (Other), Directive (Other). Captain can mark multiple objectives; marks change the pass, never the tier. A commitment bonus applies within a tier only.

Authored scoring combines urgency, expected progress, observed risk, travel and resource costs, and commitment. Missing evidence is not physical truth. Infeasible work does not block lower priorities or change global objective status. A new valid support request advances work; waiting for its reply does not monopolise a Controller. Higher-ranked work uses ordinary action cancellation without undoing physical progress.

Important information travels in scoped reports with matching readable prose and typed payload, provenance, revision, uncertainty and validity. Requests, acceptance and completion remain distinct. Delivered private reports affect their recipients, not every Console. New reports and physical outcomes become decision inputs on the next tick.

Teams know all physical facts in their current compartment through local observations. Controllers choose destinations from known work; local workflows select repairs and patients after arrival. Idle automatically assigned teams return to their authored home (default: fitted starting compartment), without searching hidden remote state. Explicit human orders retain authority.

## Native selector migration inventory

| Selector | Classification and required boundary |
| --- | --- |
| Interdiction movement, targeting, generator and departure orders | Policy: replace with published objectives executed by ordinary Controllers |
| Automatic repair destination | Policy: choose reported work or home; no remote physical fault search |
| Repair task at current location | Local workflow: complete local observation; physical work retains finite payment |
| Medical destination/order and facility selection | Policy: known assignments and facility reports; local triage remains a workflow |
| Away field-care destination | Policy: reported destination; complete information only at current location |
| Bay defend/bomb/recall | Policy: frozen craft reports and explicitly bound target intent |
| Away contribution, staffing, transport, escort and send | Department Controller policy with admitted requests/receipts |
| Scanner objective target | Permitted calculation over published objectives/requests, not finding definitions/events |
| Point-defence candidate ranking | Permitted calculation over observed trajectories, never actual projectile intent |
| Craft readiness and guidance inputs | Scoped factual producer, frozen before policy evaluation |
| Navigation assessment | Approved shipwide factual producer; does not issue commands |
| Admitted docking assistance | Physical execution of an accepted operation |
| Resource allocation, routing, treatment and finite work payment | Physical execution; may refuse work but cannot invent strategic assignments |

## Delivery

Pending. Verify each logical migration through synthetic production-loader integrations, preserve continuation deterministically, and record native/WASM/network and manually invoked smoke evidence independently. Do not infer completion from this document or from compilation.
