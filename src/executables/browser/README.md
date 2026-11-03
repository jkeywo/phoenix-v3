# Browser role composition

`web.cpp` is compiled for server, client and Workshop roles. It composes Application, presentation and the appropriate lobby/session contracts; host-only imports remain conditional. The threaded server path owns background authoritative pumping, while client/Workshop stay free of simulation implementation dependencies.

Frame timing comes from `phoenix.diagnostics.browser_profile`. Only the threaded server supplies the optional background-metrics callback. Disposable Workshop testing uses `phoenix.workshop.browser_test_client`, a worker request/feedback adapter; the worker host is a separate executable. EM_JS callbacks in this entry point remain platform composition glue.

Ordinary and threaded WebAssembly are separate build/runtime gates. Preserve exported callbacks, worker queues, shared-memory requirements and event/frame ordering. Browser rendering, device, accessibility and performance journeys require explicit smoke evidence; compiled exports alone do not establish them.

`phoenix_browser_host` is an explicitly built ordinary WASM worker target. Its worker owns a production lobby, attaches verified mission archives and advances the authoritative World. The smoke interface accepts transferred archive buffers and publishes lobby status, Console claims and tick counts. The game page still uses its existing session implementation. Integrating the worker requires the page's rendering and command projections and its direct peer transport adapter.

To run the [worker smoke](../../../tests/manual/browser_host_worker.html), build `phoenix_browser_host` with browser threads disabled into a `wasm` subdirectory of a scratch site. Package `content/test` as `test.mission.dat` at the site root using `phoenix_pack`. Extract `launcher.json` from `test.lobby.dat` as `metadata.json` and copy the smoke page as `index.html`. Serve the site over loopback HTTP without isolation headers. Open the page, start attachment, and exercise the page input button while the worker prepares.

The 3 November 2026 Chromium smoke used the 20,360,184-byte test mission. The browser reported no shared memory. The worker accepted a Console claim before download, rejected an invalid archive, retained the claim after valid attachment and advanced the World. The page accepted one input while preparing and recorded 187 animation frames during the 3.11 seconds from download to readiness. The largest reported frame gap was 17 ms through the first tick. This evidence covers the standalone offline worker.
