# Effects family

Start with contracts, then the pure rule evaluator, property preparation and ability/process continuation. These modules accept explicit definitions, fixed ticks and native-owned state; they do not read a World or renderer.

Native integration is ordered by the World schedule. Physical mechanisms own actual damage, repair, clinical progress, resource allocation and deliveries. Prepared transactions copy only supported native outcome fields and commit with revision checks. See the `src/world/effect-applications/`, `effect-lifecycle/` and `effect-work/` folders and the migration ledger for native binding policy still being extracted.

`rules` deliberately has separate pure evaluator and authoritative clock-state targets. Client/content verification code can use expression evaluation without acquiring Flecs.

| Folder         | Responsibility                                                                                  |
| -------------- | ----------------------------------------------------------------------------------------------- |
| `contracts`    | Native addresses, captured event context, custom property storage and common work receipts.     |
| `rules`        | Pure expression evaluation, authoritative rule clock records and application binding semantics. |
| `lifecycle`    | Ordered event queues, periodic grants and continuation validation.                              |
| `properties`   | Property applications, aggregate preparation and native snapshot reads.                         |
| `abilities`    | Ability grants, source recovery reservations and continuation checks.                           |
| `processes`    | Native-owner process intent, fractional work and funding transitions.                           |
| `transactions` | Candidate native state, operation preparation, exact payment guards and shared commit ordering. |
| `funding`      | Upkeep bundle identities, work credits, dependency bounds and input quotations.                 |

The central migration ledger distinguishes compiled and integrated modules from prepared extractions. Native adapters compile as ordinary World binding units; moving a body there is distinct from extracting its policy into a domain owner.
