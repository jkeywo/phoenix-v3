# Native role composition

`native.cpp` retains command-line startup and joins the local or remote role to the presentation loop. Native display enumeration, assignments, spanning, layout loading, refresh polling and frame pacing are implemented by `phoenix.native.displays` in `displays.cppm` and `displays.cpp`.

The adapter receives Application at this executable boundary. It owns the display callbacks and clears them when it leaves scope; the application outlives it. Stable platform identity discovery remains below this adapter and has no Application or graphics dependency.

Existing command-line forms, one-window display routes, monitor remapping and layout validation are preserved. Native smoke journeys are explicitly invoked evidence and do not run during ordinary startup.

Smoke flags and per-journey state live in `tests/manual/native_smokes.hpp`; the independently compiled `native_smokes.cpp` dispatches existing menu, controller, layout, personnel, engineering and bridge probes at the original frame stages. It reports whether the executable should advance, retry, finish or return failure. The frame loop retains shutdown and performance-capture completion checks.

Packaged menu launches stage a host lobby from `base.lobby.dat`. Once that lobby draws, the native content job reads the paired mission archive, checks its hash and verifies the selected packs. The lobby receives the verified receipt on its owning session and keeps its room, roster and Console assignments while preparing gameplay.

The lobby archive's optional `menu-presentation.dat` provides the native menu's theme-only catalogue. Startup verifies it independently and attaches its theme/artwork to the public menu catalogue. The first completed menu frame is logged for both legacy and split packaged launches.

A fresh-profile Windows `--menu-smoke-large --local-only` run drew the packaged host lobby at 3.05 seconds from startup. Mission verification completed at 81.92 seconds. The smoke asserted that ship selection changed screens without queuing the pre-lobby content overlay, reached Ready and launched into the Viewscreen. The later navigation check stopped the run when it encountered the Viewscreen's resource-loading cancel control. The focused `system.lobby-worker-consoles` integration check passed.

A fresh-profile rerun with the same mission and bundled test archive drew the lobby at 3.14 seconds and completed mission verification at 13.21 seconds. The lobby-to-verification interval fell from 78.87 to 10.07 seconds. The old first-run library path resolved the catalogue eight times across installation, metadata generation and receipt construction. The packaged path reads the root once and verifies the enabled selection once. Its measured costs were 0.56 seconds to read and hash the mission archive, 0.01 seconds to read bundled metadata and 9.17 seconds to verify and store the selection.

The next Windows capture profiled that remaining stage. One fresh profile spent 8.78 seconds verifying and storing the selection, of which 8.54 seconds was catalogue resolution. Definition verification accounted for 6.06 seconds of that resolution. The headless trace of the same mission counted 339 image decodes inside the catalogue pass, taking 3.21 seconds. Scenario dependency checks took about 40 ms in a separate capture.

The native verifier now overlaps fitting and Controller layout checks with render validation, and catalogue-scoped image receipts reuse successful full decodes by exact encoded bytes. Two fresh-profile launches with the same archives spent 7.92 and 7.24 seconds verifying and storing the selection. Storage took about 0.23 seconds in both runs. The lobby drew at 3.27 and 2.90 seconds from startup, and mission verification finished at 12.14 and 10.98 seconds. Both launched into the Viewscreen before the existing post-launch cancel-control assertion.

`workshop_bridge.cppm` exports `phoenix.native.workshop_bridge` (target
`phoenix_native_workshop_bridge`). Its out-of-line pump owns the native disposable
test-host lifetime and preserves request, elapsed-time and feedback order. The
native executable imports it; browser/Workshop UI code does not acquire that
host dependency. Manual frame capture is similarly owned by the diagnostics
native-profile interface rather than a cross-folder implementation header.
These new adapters require compile/link checkpoints separately from graphical
or disposable-test smoke evidence.
