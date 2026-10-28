# Workshop documents

Read types.cppm (phoenix.content.documents) first.

Editable drafts, dependencies, preview resolution, document operations and undo/redo history. `workshop.cpp` implements editing, loading, preview and export. `accessors.cpp` owns document access and history availability predicates; the public interface declares these methods without exporting JSON indexing implementations.

Dependencies: Catalogue compilers, verification, asset compilation and public distribution operations; no tool UI.

Ownership: A WorkshopDocument owns its draft and history; immutable loaded Pack identities are never reused for mutations.

Validation: Content/Workshop document, draft history, preview and export integration cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
