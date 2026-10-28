# Native command envelope shape

`phoenix.control.native_commands` owns the common payload-shape check for native workflow commands. `NativeCommandEnvelope` declares allowed Controller/speaker fields and an optional text byte limit. Channel/request/argument payloads use their dedicated Controller paths and are rejected by this native envelope.

This module grants no authority. Callers still validate command/action identity, ownership, generation/revision, numeric ranges, workload and physical eligibility. Coordinated impulses require request identities and deliberately retain their dedicated checks. Policies are typed C++ data, not an expression language or a replacement for host admission.
