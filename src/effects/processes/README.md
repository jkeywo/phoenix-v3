# Process execution state

`phoenix.effects.processes` owns process continuation and the state transitions used by machinery and finite work. It owns prepaid-work arithmetic, grant identities, funding pause/resume/stop transitions and work receipts. Native targets retain physical progress and the caller retains atomic transaction ownership.

Read `processes.cppm` before the implementation. Inputs are explicit immutable definitions, mutable native-owned process state and fixed tick values; no operation reads World state. Existing serialization, including the schema-31 completion alias, is preserved. Relevant system cases are processes, rules, store effects, repair/Medical, craft and FTL.
