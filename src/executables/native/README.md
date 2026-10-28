# Native role composition

`native.cpp` retains command-line startup and joins the local or remote role to the presentation loop. Native display enumeration, assignments, spanning, layout loading, refresh polling and frame pacing are implemented by `phoenix.native.displays` in `displays.cppm` and `displays.cpp`.

The adapter receives Application at this executable boundary. It owns the display callbacks and clears them when it leaves scope; the application outlives it. Stable platform identity discovery remains below this adapter and has no Application or graphics dependency.

Existing command-line forms, one-window display routes, monitor remapping and layout validation are preserved. Native smoke journeys are explicitly invoked evidence and do not run during ordinary startup.

Smoke flags and per-journey state live in `tests/manual/native_smokes.hpp`; the independently compiled `native_smokes.cpp` dispatches existing menu, controller, layout, personnel, engineering and bridge probes at the original frame stages. It reports whether the executable should advance, retry, finish or return failure. The frame loop retains shutdown and performance-capture completion checks.

`workshop_bridge.cppm` exports `phoenix.native.workshop_bridge` (target
`phoenix_native_workshop_bridge`). Its out-of-line pump owns the native disposable
test-host lifetime and preserves request, elapsed-time and feedback order. The
native executable imports it; browser/Workshop UI code does not acquire that
host dependency. Manual frame capture is similarly owned by the diagnostics
native-profile interface rather than a cross-folder implementation header.
These new adapters require compile/link checkpoints separately from graphical
or disposable-test smoke evidence.
