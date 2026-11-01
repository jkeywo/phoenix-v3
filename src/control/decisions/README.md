# Permitted Controller decisions

`phoenix.control.decisions` owns observation binding, factual decision inputs, candidate preparation and objective arbitration over immutable permitted snapshots. `decisions.cppm` owns `ControllerDecisionContext` and `ControllerObservationSources`; callers import it directly.

`objective_choices.cpp` performs shared evaluation, integer ranking, commitments, retry assessment and bounded diagnostics. `candidate_adapters.hpp` is a private typed collaborator contract: each source adapter owns enumeration, objective scope, target identity, feasibility reasons and evidence capture. Domain adapters live with their candidate/fact implementation; Away, craft and addressed-request producers remain in `candidate_adapters.cpp`. Adapters retain no input references beyond the evaluation.

`phoenix.control.objectives` has the independent `phoenix_control_objectives` target for scoring alone. Neither scoring nor decision preparation links the action interpreter, mutable World runtime, Flecs, providers or presentation. Geometry helpers and explicit permitted definitions are lower collaborators, not authority.

Action progression, cancellation, admission, receipt mutation and Controller serialization stay in `../execution`. Pure report identities belong with Controller continuation in `../controllers`; retained evidence stream identity/quality belongs to Knowledge records. World captures frozen/fallback inputs, restricts them to the addressed Controller and enriches retained inspections synchronously before supplying this owner.

Validation uses existing synthetic Controller, Knowledge, sensors, personnel, delivery, Medical, repair, craft, Away and FTL integrations. Architecture fixtures also refuse explicitly allowed reverse dependencies. See `docs/module-ownership-followup.md` for the actual executed gates.
