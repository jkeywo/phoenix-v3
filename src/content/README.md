# Content compilation

Content turns authored packs into immutable, typed definitions. It owns neither an authoritative World nor a renderer. Workshop drafts use the same compilation and verification paths without becoming verified merely because a preview can resolve them.

Read the family in this order:

1. `packs/` and the independent image codec in `assets/`: bounded archives and immutable source identities.
2. `catalog/`: catalogue records, dependency metadata, inheritance and knowledge composition.
3. Domain `types.cppm` interfaces: compiled definitions, without catalogue lookup or execution.
4. Domain `compiler.cppm` interfaces and their implementations: compilation against one resolved catalogue.
5. `verification/`: prepares and verifies the complete dependency closure.
6. `distribution/`: creates permitted Console/client assets and public launcher metadata from verified content.
7. `documents/`: mutable Workshop documents, history and draft previews.

Each declaration has one named-module owner. Compiler interfaces import catalogue/definition contracts, not other compiler interfaces; implementation imports record the operations they actually call. A few definition interfaces also own generic value validation and immutable descriptor operations used by both compilation and execution.

The compiler implementations currently contain genuine recursive relationships: directed/combat profiles, rules/effects/abilities/processes, and stocks/logistics. They therefore share the deliberate `phoenix_content_compilers` static target, while retaining distinct public named modules and implementation files. The interface graph is acyclic. Do not introduce target cycles, import the aggregate from a compiler, or pretend the shared implementation target is an independently linked domain package.

Packs, image codecs, catalogue operations, UI layout, asset compilation, distribution and Workshop documents have separate targets. None links Flecs, World execution, raylib rendering, RmlUi or ImGui. Their private third-party image/model/audio decoders are content verification facilities, not graphics ownership.

Production and test callers import the narrow named interface they use. The former catalogue-wide re-export facade and its build target have been retired; declaration ownership remains with the domain interfaces above.

For build ownership, link `phoenix_content_<domain>_types` when only compiled domain definitions are needed. Link `phoenix_content_compilers` when calling a domain compiler; its distinct named interfaces remain the API entry points. Pack, catalogue, assets, image codec, UI, distribution and documents each have their own `phoenix_content_<module>` target. Distribution is above verification/compilation; packs and the image codec remain below them.

The existing content integration cases, production pack verification, domain loader cases, Workshop document cases and native/WASM continuation cover this family. No schemas, tuning, archive hashes or validation rules are intentionally changed by this extraction.
