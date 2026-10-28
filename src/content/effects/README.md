# Effect definitions

Read compiler.cppm and types.cppm first.

Effect, rule, ability, process and custom-property compilation.

Dependencies: Catalogue and typed domain contracts; native descriptors and scalar validators depend only on leaf definitions.

Ownership: types.cppm owns immutable definitions; native_properties.cpp and scalars.cpp provide shared descriptor/value operations. Rule evaluation belongs to phoenix.effects.rules.

Validation: Effects, rules, custom properties, abilities, processes and finite-work integration cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
