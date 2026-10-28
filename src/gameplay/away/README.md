# Away operations

`phoenix.gameplay.away.state` owns away phases, medical-care continuations and mission state. Phase predicates and canonical serializers are independently compiled in `serialization.cpp`. Boarding continuation state has its own owner and is composed into a mission.

The existing mission schema and its defaulting behavior are preserved. State-tree continuations and medical order continuations retain their separate validated save/restore paths. World adapters still bind missions to entities, craft, personnel, conversations and finite work.

`phoenix.gameplay.away.operations` owns the aligned definition/mission registry, stable save ordering, sequence allocation, reservation and transfer arbitration, deployment/recovery transitions, personnel disposition precedence and lost-party decisions. Definitions are read-only to consumers; bounded mission spans permit record updates while collection initialization and appends stay with the owner.

`phoenix.gameplay.away.crew` owns navigator qualification, crew health and slot eligibility from typed personnel, controller and equipment definitions. Native adapters retain authority checks and located component access. These three owners are active in the native integration graph; the native checkpoint includes the existing away, personnel, medical, boarding and save/restore paths. Browser validation is a separate gate.

`phoenix.gameplay.away.runtime` builds on the same mission book. `runtime.cpp` owns staging, transfer/lifecycle advancement, deployment/recovery, committed work progression and ordered state-tree outputs. Readonly observations and execution actors are separate; native queries delegate to these canonical policies without acquiring mutable components or constructing peer contexts.

`persistence.cpp` owns schema 11 save/strict restore, medical continuation validation and incremental record installation. Reservation and slot eligibility serve both restore and native command callers. Boarding capture/defender receipts remain validated by the existing boarding owner at the original point in restore.

`projection.cpp` gates origin and Hidden disclosure, then formats canonical mission fields from typed permitted rows. The readonly native collector retains control/physical choice checks and their order. Automatic command contributions, transport blocker policy and cross-domain operations remain explicit native composition in `src/world/bindings/gameplay_away_01.cpp`, `_02.cpp` and `_03.cpp`. Runtime actor implementations live privately in `src/world/lifecycle/runtime.cppm`; no mutable World context is exported.

The Runtime providers have passed standalone compilation. The coupled native persistence/projection integration is activated for its final semantic gate; browser validation remains separate. The native collector and peer operations are unfinished ownership seams, so this does not establish full Away command extraction.
