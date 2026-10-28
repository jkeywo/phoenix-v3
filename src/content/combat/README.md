# Combat definitions

Read compiler.cppm and types.cppm first.

Compiled compartments, weapons, magazines, shields, directed modes and payload declarations.

Dependencies: Resource, operation and effect definition contracts; compiler collaborators are explicit private imports.

Ownership: Definitions contain physical parameters and immutable payload references, not mutable combat state.

`serializers.cpp` owns continuation encoding for captured projectile/destruction definitions. Its exported declarations live with those types; consumers must not generate competing serializers in their own modules. Missing fields use a fresh default definition, preserving the previous continuation contract.

Validation: Combat, directed-system, shields and loader verification cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
