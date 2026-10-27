# Custom properties and production psychic strain

Approved seventh gameplay-engine migration slice. Implementation starts from `3f736a6`, after the completed shared-rules migration and ability execution consolidation. Machinery processes remain the next slice.

## Contract

The mod-extensible property catalogue supports quantities, numeric attributes, flags and enums. Explicit declarations on actors, equipment, compartments and officers initialise stored values once; missing declarations remain missing. Quantities are nonnegative stored balances changed explicitly or spent. Attributes combine the existing additive percentage contributions against their base. Flags and enums permit temporary overrides, highest authored priority then newest distinct application; refresh preserves precedence. Expiry reveals the current base or next contribution. Enum domains, numeric units, bounds and display metadata are checked during content compilation.

Use existing native owner identities and ECS components or native officer/compartment records. Do not create proxy entities, a global blackboard or another source registry. Catalogue definitions are compiled once per content set.

Shared typed expressions distinguish base and effective values. Prepared effect transactions stage typed set, add, subtract and rate changes together with existing resource payments and contributions. Failure leaves all balances and events untouched. Out-of-bounds mutation rejects unless saturation is explicitly authored; costs never saturate. Integer rate remainders survive continuation and quotation retries. Quantity costs join existing one-off and ongoing prepared work, including mixed resource bundles, canonical aliases, saved credits and existing shortage policies. Outputs cannot finance their own step.

Property-change events carry coalesced base/effective before and after values and enter the existing bounded phase queue. Temporary contribution changes emit when effective values change; refresh alone does not. No recursive execution is introduced.

## Abilities and psychic strain

Shared expression requirements apply at admission, preparation completion and ongoing operation. An authored interruption policy ends execution normally while retaining sustained intent. Subsequent starts are fresh activations with ordinary readiness, exclusions, funding and cooldown checks. Completed-work receipts freeze performer, activation and actual funded, fault-adjusted output. Authored family-filtered rules use the same work totals for gain and idle recovery.

Each psychic officer owns strain, following reassignment. Production defaults are strain 0, capacity 60,000, full-output gain 2,000 per second, idle recovery 4,500 per second, safe-resume fraction 250 permille and state `ready`/`recovering`. Concurrent psychic output adds gain; no psychic output permits recovery, including travel and unassignment. Living owner lifetime is explicit and does not loosen duty-buff eligibility.

At capacity, shared authored rules latch the stored recovery state and end all that officer's psychic work, retaining requests. At or below 25% of effective capacity they clear the latch. Rules read stored state where necessary so overrides cannot hide a stale latch. Initial pacing is 30 seconds from fresh, 10 seconds resting to resume and 22.5 seconds subsequent use; content/mods own tuning. Native cloak/ward/ghost retirement remains authoritative. No random injuries, Medical dependency or special native fatigue scheduler is added.

## Authoring, information and continuation

Workshop supports definitions, declarations, typed operations, requirements, costs and rules through the existing content path. Localised generic gauges/flags/enums use existing department and Console permissions. Authorised AI receives the same permitted frozen information. Neither private bases nor provenance are implicitly published. Psychic status accompanies existing requested/actual power status; automatic ward retains its intent while recovering.

Persist stored bases, contribution identities/order, rate remainders, prepaid credits, pending events and interruption state. Rebuild derived values without replay. Advance World and portable session schemas once from the completed predecessor; incompatible saves reject. Update the previous no-fatigue design statement and authoring/roadmap evidence.

## Delivery and verification

Deliver reviewable verified steps for catalogue/owners/saves, operations/contributions/events, mixed costs/ability work, then production content and UI. Synthetic production-loader fixtures cover all four owners, missing/types/bounds, signed attributes, quantity conservation/rollback, overrides and refresh/expiry, deferred events and fractional rates, mixed competing costs and electrical allocation, partial/zero/concurrent psychic work, reassignment/intent/cooldowns, and continuation at consequential boundaries. Verify production content without freezing balance values.

Obtain independent correctness review and an architecture audit, fix actionable gaps and re-review. Run focused checks, then complete native and threaded WebAssembly gates, ordinary WebAssembly property/ability checks and real production preflight with matching continuation traces. Keep network timing checks separate from expensive compilation. Record actual revisions, commands and limitations. GUI, screen-reader, physical device and performance acceptance remain manual.
