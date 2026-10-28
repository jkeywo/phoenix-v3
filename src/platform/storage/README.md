# Persistent blobs and peer journals

Start at `blobs.cppm` and `storage.cppm`.

BlobStore owns asynchronous immutable byte payload persistence. PeerStore owns bounded journal encoding/replay and one outstanding durable transaction. Windows/POSIX file backends implement a private JournalFile contract; native_store.cpp owns the single worker/queue implementation. Browser stores use IndexedDB and main-thread service dispatch.

Allowed dependencies: Blobs: portable data. Journals: consensus durable transaction contracts. Browser adapters additionally use platform/lifecycle. No content catalogue, UI or World execution.

Ownership and lifetime: Native journal locks out a second writer, recovers only complete records and flushes before acknowledgement. Checkpoint replacement retains ownership locking. Browser callbacks retain exported names and reject late results after disposal. Workers join before owned resources are released.

Relevant validation: `system.peer-storage`, `system.persistent-content`, consensus recovery; IndexedDB reload/exclusive locking remains manually invoked evidence. These identify checks to run; this README is not execution evidence.
