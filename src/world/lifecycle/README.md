# World construction

`phoenix.world.assembly` composes immutable entity definitions from the catalogue and explicitly supplied immutable effect/resource catalogues. Its output contains fitted/native definition templates, visual scale and communication participant templates. It imports content compilers and typed records, with no World, Flecs registry or mutable runtime context.

World installs those records into its authoritative registry in the existing entity order and retains exception-safe native child construction. The facade also composes the explicit tick phases and staged candidate restore. Domain runtime algorithms belong to their gameplay/effects/control/knowledge owners; remaining included native integration fragments are ongoing migration work.

Assembly is prepared but not activated until the coordinated compilation checkpoint.
