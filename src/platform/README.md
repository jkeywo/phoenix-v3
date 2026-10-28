# Platform family

Each module adapts one service to native/browser facilities while retaining a shared C++ contract. Start with lifecycle for worker-to-browser main-thread dispatch, then the service being used: devices, files, storage or transport. Accessibility is paired with the shared UI semantic model and has its own adapter owner.

Files owns user-facing import/export/clipboard operations. Storage owns persistent byte blobs and peer journals; networking/durability owns consensus coordination. Transport owns peer channels, signalling policy and bounded public-content downloads. Native/browser implementations live beside their interface rather than forming separate gameplay frontends.

No platform service grants Console authority. Actual device, screen-reader, IndexedDB reload and public-network acceptance remains separately invoked smoke evidence; compile/system results do not establish those requirements.
