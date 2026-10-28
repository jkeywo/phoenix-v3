# Disposable authoritative Workshop host

`workshop_test.cppm` exports `phoenix.workshop_test`; link `phoenix_workshop_test_host`. GameplayTestHost accepts bounded start/pause/step/restart/stop requests, resolves the supplied draft/dependencies through production content APIs, owns a disposable World and exposes feedback. `workshop_test.cpp` implements that lifetime.

`main.cpp` is the browser-facing worker executable and exported byte/request/feedback entry points. It is separate from the browser Workshop UI: authoring queues a request to the worker rather than linking authority. The native executable uses its own host-pump adapter, preserving the same request semantics.

Existing Workshop-host and content cases cover invalid bundles, restart and stepping. Browser worker start/pause/step/restart is a separate manual journey; captures, worker execution and role build gates must be recorded separately in migration evidence.
