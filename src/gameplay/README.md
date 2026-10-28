# Native gameplay mechanisms

Begin with `fitting/` for canonical equipment identity and native component views, then the domain you are changing. `flight/` owns motion commands/guidance/work; `combat/` supplies reusable state, shield/fault and candidate-outcome mechanisms. Weapons, directed, damage, ordnance/contacts, repairs and continuation have their own focused owners rather than one combat implementation.

Personnel, teams, boarding/away, logistics, craft, docking, FTL, sensors and psychic machinery each consume typed definitions and native facts. Effects owns shared invocation/funding/grant mechanisms; resource adapters own accounting; World chooses authoritative phase order and actor lifetime. Presentation consumes permitted projections rather than calling these mechanisms directly.

Public entry points are the named interfaces described in each module README. Native integration now compiles in ordinary workflow units under `src/world/` rather than included class-body headers. This fixes the compilation boundary; remaining domain policy there is explicitly documented integration ownership. The flight native service is adopted through bounded ports, with its compilation and behavioral receipts recorded separately.

Existing synthetic gameplay/resource/effect/Controller journeys and exact continuation traces are the relevant gates. The [migration evidence](../../docs/modularisation-evidence.md) records completed native and ordinary/threaded WASM gates and remaining environmental validation; reading guides do not establish acceptance on their own.
