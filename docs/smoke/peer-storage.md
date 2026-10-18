# Peer storage smoke record

18 October 2026 checkpoint extension: the production browser probe at `http://127.0.0.1:8087/storage-probe.html?key=checkpoint-20261018-a` committed its record, replaced the IndexedDB journal in a strict transaction and reported success. The reload action (`recover=1`) recovered the compacted checkpoint. A simultaneous second tab was refused by the exclusive Web Lock; after the first tab closed and released its lock, reloading the second recovered the same checkpoint. This checks actual Chromium IndexedDB, separately from Node/WASM tests. Native integration additionally checks that atomic replacement reduces the journal and keeps writer exclusion through replacement.

17 October 2026. Built the C++ browser storage probe with the production consensus actor, incremental transaction codec and browser IndexedDB adapter. Served `build/web` over loopback and opened `storage-probe.html?key=phoenix-integration-20261017-a` in the in-app Chromium browser.

- The probe elected a synthetic singleton, committed its test record through a strict durable IndexedDB transaction and reported success.
- Reload recovered the previously committed record. The explicit reload action added `recover=1`; this path refuses a missing record and cannot pass by silently creating a new one.
- With the successful probe retaining its store, a second tab using the same namespace reported that the peer identity was already open in another tab.
- Closing the first tab and reloading the second allowed recovery of the original committed record. Its browser warning/error log was empty.

This proves the exercised browser's transaction/reload/exclusive-writer path. It does not establish resilience to physical power loss, browser-origin eviction, user-cleared storage or networked fleet recovery. The storage namespace here is synthetic test state, not a live player identity.

The automatic native `system.peer-storage` case uses a temporary journal, writes through the real worker/flush path, restarts the consensus actor, repairs an incomplete unacknowledged tail, rejects a corrupt complete record and refuses concurrent writers. Six native system cases and headless WASM content/simulation/session/replication cases pass. No frame-time or production-content tuning assertions are added.
