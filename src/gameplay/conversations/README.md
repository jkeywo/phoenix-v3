# Conversations

Start with `conversations.cppm` (`phoenix.conversations`). `conversation_definitions.cpp` compiles authored dialogue programs; `conversations.cpp` owns dialogue instances, participants, choices, message history and continuation. World/Console adapters provide legitimate endpoints and submit admitted interactions.

`phoenix_gameplay_conversations_definitions` links foundation and state-tree definitions. `phoenix_gameplay_conversations` adds state-tree execution. The generic conversation runtime has no World or UI dependency.

Retain participant visibility, linked endpoint rules, prompt/choice identity, expiry and resumed tree progress. Existing conversation, Comms and away-operation integration cases cover those contracts. A browser/native conversation UI journey remains separate smoke evidence.
