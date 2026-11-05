# Permitted content distribution

Read compiler.cppm (phoenix.content.distribution) first.

Console/client asset bundles, public setup metadata and companion distribution manifests.

Dependencies: Verified catalogue resolution, asset compilation and UI definitions.

Ownership: Bundles preserve exact source identities and permitted visibility; launcher metadata grants no admission or authority.

Split lobby archives include a standalone menu presentation DAT with the resolved menu theme and its referenced artwork/fonts. It excludes mission definitions and the Console background. Native startup validates this small presentation pack and keeps it with the public menu catalogue so drawing the menu never needs to open installed mission archives.

`PresentationCache` prepares the permitted draft before accepting a packaged `.clients` companion. Validation compares the complete manifest/entry roster, definitions, non-image bytes and decoded image dimensions/pixels. A failed or absent companion compiles the current draft once. Keys include ordered source identities, ship, effective draft/background content and compiler version. Active session preparations retain their immutable results independently of bounded cache eviction.

A private weak index keeps those active results discoverable by their cache and content key after eviction. It owns no bundles and prunes expired references, so handoff cannot accidentally create a second preparation while a caller still retains the first.

Public descriptors expose only HTTP(S) locations. Private artifact lookup may derive a native or browser location from an explicitly configured source hash/location pair; it does not turn a peer-supplied path into file access.

Validation: Content/asset bundles, launcher and network content integration cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
