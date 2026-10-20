# Manual Helm verification

Date: 20 October 2026.

Manual Helm adds a combined mode toggle, momentary yaw/thrust input, assigned gamepad analogue axes, keyboard routing and a captured virtual joystick. Its operating contract is documented in [Gameplay Consoles](../gameplay-consoles.md#manual-helm-control).

## Automated contracts

`system.input-and-semantics` tests independent signed axes, source ownership, neutral-before-takeover, interruption, real centring after gamepad assignment, the shared dead zone and disconnect release. These are synthetic routing checks, without physical controllers.

`system.gameplay-flight` loads independent test-only mechanisms through production content/session paths. It covers separately bound engine/steering Controllers, confirmed mode transitions, arbitrary proportional effort, reverse, release retaining momentum, assistance and human/AI authority. Existing flight cases cover resource limits, navigation cancellation and physical conservation.

## Explicit graphical journey

The opt-in smoke target runs the actual Console renderer/model against the authored Courier. It selects Manual, moves and releases a captured virtual joystick outside its bounds, injects keyboard and analogue samples through the presentation handlers, checks committed telemetry, verifies input sampling limits and captures the rendered Console. This is programmatic graphical evidence, not physical keyboard, touch or gamepad acceptance.

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_manual_flight_smoke
./build/native/Release/phoenix_manual_flight_smoke.exe build/native
./build/native/Release/phoenix_manual_flight_smoke.exe build/native portrait
./scripts/build.ps1 -Target web -BuildTarget phoenix_manual_flight_smoke
```

Serve `build/web` and open `phoenix_manual_flight_smoke.html`; add `?portrait` for portrait layout. The browser publishes its result in the page and retains `Module.manualFlightCapture`. Native writes `manual-flight-console.png` in the supplied pack directory. This target is excluded from ordinary builds and automatic tests.

Physical gamepad/touchscreen operation, actual screen-reader use and six-display performance require separate manual runs. No such acceptance is inferred from the synthetic or graphical programmatic journeys.

## Recorded results

The native landscape and portrait graphical journeys passed and their captures were visually inspected. The browser landscape and portrait WebGL journeys passed and exposed the Manual toggle, pulse actions and axis readout in its accessibility tree. All inputs in these journeys were deliberately injected by the smoke tool.

The complete gate passed all 22 native system cases, seven signalling/real-transport journeys, WASM integration cases and native/WASM continuation comparison. Build/test evidence is retained in `build/manual-flight-final-gate.log`; graphical evidence is in `build/manual-flight-smoke.log`, `build/manual-flight-portrait-smoke.log` and `build/manual-flight-browser-smoke.log`. The focused regression also verifies that a repeated Manual request takes control back after navigation physically changes assistance, despite the owned mode setting having the same stored value.
