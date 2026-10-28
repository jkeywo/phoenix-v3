# Effect contracts

`phoenix.effects.contracts` owns native effect addresses, captured context, prepaid work and outcome records shared by effect execution and its physical adapters. It also owns bounded tree-effect receipt identities, source identity validation and stable rule/owner identities.

The records have no World, renderer or Flecs dependency. Their native continuation fields and JSON encoding are unchanged. Native mechanisms continue to own actual physical state; outcome records stage only the fields a transaction may commit.

Read `contracts.cppm` before `contracts.cpp`. Relevant integration cases are rules, abilities, processes, custom properties, store effects and Controller runtime.
