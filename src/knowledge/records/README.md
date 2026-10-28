# Knowledge records

Start with `knowledge.cppm` (`phoenix.knowledge`). `knowledge_definitions.cpp` compiles schemas, roles and immutable bindings; `knowledge.cpp` owns record retention, scoped projection, coordination delivery and continuation. Console/ship integration belongs to sibling modules rather than this generic store.

`phoenix_knowledge_records_definitions` links portable foundation only. `phoenix_knowledge_records` adds execution without importing World, application, UI or content compilation.

The runtime owns provenance, record identities, private AI memory, pending deliveries and status latches. Rebuild transient decoded-field caches on restore and preserve message/record ordering. Existing knowledge/Coordination and Controller cases cover legitimate publication, expiry, delivery and continuation.
