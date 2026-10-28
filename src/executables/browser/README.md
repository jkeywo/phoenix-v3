# Browser role composition

`web.cpp` is compiled for server, client and Workshop roles. It composes Application, presentation and the appropriate lobby/session contracts; host-only imports remain conditional. The threaded server path owns background authoritative pumping, while client/Workshop stay free of simulation implementation dependencies.

Frame timing comes from `phoenix.diagnostics.browser_profile`. Only the threaded server supplies the optional background-metrics callback. Disposable Workshop testing uses `phoenix.workshop.browser_test_client`, a worker request/feedback adapter; the worker host is a separate executable. EM_JS callbacks in this entry point remain platform composition glue.

Ordinary and threaded WebAssembly are separate build/runtime gates. Preserve exported callbacks, worker queues, shared-memory requirements and event/frame ordering. Browser rendering, device, accessibility and performance journeys require explicit smoke evidence; compiled exports alone do not establish them.
