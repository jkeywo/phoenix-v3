# Away operations

`phoenix.gameplay.away.state` owns away phases, medical-care continuations and mission state. Phase predicates and canonical serializers are independently compiled in `serialization.cpp`. Boarding continuation state has its own owner and is composed into a mission.

The existing mission schema and its defaulting behavior are preserved. State-tree continuations and medical order continuations retain their separate validated save/restore paths. World adapters still bind missions to entities, craft, personnel, conversations and finite work.
