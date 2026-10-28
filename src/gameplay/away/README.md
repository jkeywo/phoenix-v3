# Away operations

`phoenix.gameplay.away.state` owns away phases, medical-care continuations and mission state. Phase predicates and canonical serializers are independently compiled in `serialization.cpp`. Boarding continuation state has its own owner and is composed into a mission.

The existing mission schema and its defaulting behavior are preserved. State-tree continuations and medical order continuations retain their separate validated save/restore paths. World adapters still bind missions to entities, craft, personnel, conversations and finite work.

`phoenix.gameplay.away.operations` owns the aligned definition/mission registry, stable save ordering, sequence allocation, reservation and transfer arbitration, deployment/recovery transitions, personnel disposition precedence and lost-party decisions. Definitions are read-only to consumers; bounded mission spans permit record updates while collection initialization and appends stay with the owner.

`phoenix.gameplay.away.crew` owns navigator qualification, crew health and slot eligibility from typed personnel, controller and equipment definitions. Native adapters retain authority checks and located component access. These three owners are active in the native integration graph; the native checkpoint includes the existing away, personnel, medical, boarding and save/restore paths. Browser validation is a separate gate.

Staging, transport blockers and parts of mission advancement still contain domain policy in the native adapter. Their location in this folder does not establish independent ownership.
