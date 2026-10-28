# Native gameplay mechanisms

Begin with `fitting/` for canonical equipment identity and native component views, then the domain you are changing. `flight/` owns motion commands/guidance/work; `combat/` supplies reusable state, shield/fault and candidate-outcome mechanisms. Weapons, directed, damage, ordnance/contacts, repairs and continuation have their own focused owners rather than one combat implementation.

Personnel, teams, boarding/away, logistics, craft, docking, FTL, sensors and psychic machinery each consume typed definitions and native facts. Effects owns shared invocation/funding/grant mechanisms; resource adapters own accounting; World chooses authoritative phase order and actor lifetime. Presentation consumes permitted projections rather than calling these mechanisms directly.

Public entry points are the named interfaces described in each module README. Some folders still contain World-included integration fragments; those remain explicit migration work and must not be treated as separately compiled public APIs. The new flight native service is a provider until its guarded World adoption and continuation checks pass.

Existing synthetic gameplay/resource/effect/Controller journeys and exact continuation traces are the relevant gates. The migration evidence records actual checks and pending browser/World/platform work; reading guides do not establish acceptance on their own.
