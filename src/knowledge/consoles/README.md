# Console knowledge integration

`phoenix.knowledge.consoles` owns authored ship/Console binding identities, trusted telemetry ingestion, scoped evidence, publication ownership, addressed messages and their continuation. `consoles.cppm` defines `GameplayKnowledge`; `gameplay_knowledge.cpp` implements it independently of World and Flecs.

It composes generic knowledge records/runtime and production content compilation. Configure definitions before registering ships or restoring; restoration rebuilds identities and clears live human authority before delivery. Physical systems feed legitimate observations; human/AI messages share authority checks. Relevant integration: knowledge, Controller coordination, scenarios and session continuation.

`latest_shared_record` is a trusted direct-publisher lookup, not a Console/client observation API. It preserves the typed stream and withdrawal independently of `public_board` truncation. Camera bindings compile during configuration and are retained when rebuilding a restored adapter.
