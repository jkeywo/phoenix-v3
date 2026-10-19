# Disposable gameplay Worker smoke

Manually triggered on 19 October 2026 using the local compiled web build in the
in-app Chromium browser. The probe uses the actual `workshop-test-worker.js`, its
headless C++/WASM host, and the verified Base and Training archives. It does not
replace the automated synthetic system cases.

The powered navigation scenario started with `base.courier-engineering`, ordinary
System controllers, two independent scenario controllers, a scoped reach-region
objective and finite resource telemetry. No host diagnostics were reported.
Pause held tick 221; Step advanced to tick 222 and remained paused. Restart
reconstructed the exported snapshot and resumed. Stop returned `running:false`,
`paused:true`, and `Disposable test stopped.` with an empty diagnostic list.

The first run exposed a large-upload call-stack failure and heartbeat feedback
overwriting the failure. Uploads now use bounded heap scratch memory with explicit
heap exports, and errors remain visible until a successful request. The complete
lifecycle above passed after rebuilding those fixes.

To repeat, copy `tests/manual/workshop_worker.html` beside the built web assets and
serve that directory over localhost. The probe is deliberately excluded from site
publication. This establishes actual browser Worker loading and execution. It does
not establish physical input/display performance or audible screen-reader behavior.

## Graphical Workshop edit and test

The compiled browser Workshop also ran on localhost. Selecting `base.cruiser`
resolved its assembled fitted identities and controller bindings. This retry
verified the preview fix: equipment resolution now includes local and dependency
controller trees instead of reporting a false missing-tree error.

In Templates and assemblies, the engine template's acceleration was changed in the
temporary draft. **Verify gameplay draft** reported successful production-loader
verification. Test exported that edited draft, selected Free Flight, the Cruiser
and `base.player`, and started the isolated World. Live feedback included controller
states, `resource_grants`, `pending_outputs`, scoped boards and the world clock.
Stop disposed of that World. An earlier graphical run also paused the Cruiser and
displayed its retained tick. The draft was not saved over repository content.

This is an actual graphical edit → verify → export → load → run → dispose journey.
It does not claim every inspector interaction, inherited override combination,
physical controller/touch routing, audible reader interaction or tuning quality
was manually exercised.
