# World construction

`phoenix.world.assembly` composes immutable entity definitions from the catalogue and explicitly supplied immutable effect/resource catalogues. Its output contains fitted/native definition templates, visual scale and communication participant templates. It imports content compilers and typed records, with no World, Flecs registry or mutable runtime context.

`assembly_bindings.cpp` installs those records into the authoritative registry in the existing entity order and retains exception-safe native child construction. The public facade is `simulation.cppm`; `world.cpp` constructs and destroys its opaque implementation. The private `:runtime` partition declares the canonical Runtime and preserves native field order. `instance.cpp` defines its destructor out of line, keeping JSON/container destruction in its owning compilation unit.

Ordinary native methods compile in `world/bindings`; the explicit tick sequence is in `world/schedule`, and candidate restoration is in `world/persistence`. The private partition retains required templates, small shared helpers and bounded nested actor adapters. No domain module imports that mutable implementation; independently compiled owners receive typed actors, immutable definitions and bounded peer operations.

The real Runtime split passed MSVC compilation and focused simulation, controller and effects integration cases. Full native and browser evidence is tracked separately. Remaining policy adoption is distinct from this verified compilation boundary.
