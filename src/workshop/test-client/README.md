# Disposable test client

`phoenix.workshop.test_client` owns selection of mission/ship/slot, bounded UI
feedback, pending GameplayTestRequest and explicit smoke-step state. Its interface
is `test_client.cppm`; it prepares a verified draft/dependency bundle and queues a
request rather than executing simulation.

Application composition delivers requests to the existing native test host or
the browser adapter in `browser_workshop_test.cpp`. Browser callbacks retain
their public names. The host remains a separate executable/authoritative target,
and no authority enters the browser Workshop UI through this module.

Existing Workshop-host, content and network transfer system cases remain the
behavioural checks. A browser Test start/pause/step/restart journey is manual smoke.
