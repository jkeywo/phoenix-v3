# Disposable test client

`phoenix.workshop.test_client` owns selection of mission/ship/slot, bounded UI
feedback, pending GameplayTestRequest and explicit smoke-step state. Its interface
is `test_client.cppm`; it prepares a verified draft/dependency bundle and queues a
request rather than executing simulation.

Application composition delivers requests to the existing native test host or
`phoenix.workshop.browser_test_client`, whose front door is
`browser_test_client.cppm` (target `phoenix_workshop_browser_test_client`). The
browser adapter owns worker transfer/feedback in `browser_workshop_test.cpp`
and imports the tools request API; tools no longer owns that implementation.
The adapter sits above application composition, while the lower generic test
client retains its document/UI responsibility. Browser callbacks keep their
public names. The host remains a separate authoritative executable; the browser
adapter does not link World or local simulation.

Existing Workshop-host, content and network transfer system cases remain the
behavioural checks. A browser Test start/pause/step/restart journey is manual smoke.
