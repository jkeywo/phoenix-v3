# Catalogue verification

Read compiler.cppm (phoenix.content.verification) first.

Full resolved-pack preparation, cross-domain verification and cached immutable resolution.

Dependencies: Catalogue and domain compiler APIs; no application or authoritative runtime.

Ownership: Compiled bindings and caches are rebuilt from authored content, not saved World continuation.

Catalogue preparation retains each compiled Controller policy and its verified action, candidate and objective artifacts. Repreparing an edited catalogue replaces these artifacts along with the registry and tree program.

`verify_packs` alone constructs the opaque `VerifiedPacks` receipt. It retains immutable archives and the resolved catalogue together. Live receipts may be reused for the exact ordered archive hashes; the bounded process index holds weak references. Consumers cannot manufacture a receipt or mutate its catalogue. Ordinary unverified inputs still pass through production resolution.

Validation: Content integration, production pack verification and domain loader integration cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
