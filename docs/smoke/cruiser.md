# Cruiser fit evidence

Explicit authored-content exercises on Windows, 21–22 October 2026. These runs describe this fit and build; they are not production balance assertions in CTest.

## Materials and registration

The native `phoenix_cruiser_smoke` gallery loaded the packaged production Cruiser and rendered all eight assembled bearings at 1600×900. Four captures changed the light direction and turret headings, including ventral inversion. The calibrated pack passed production resolution. Local captures are `build/cruiser-bearings.png` and `build/cruiser-lighting-{port,aft,starboard}.png`.

The hull depth draft came from the built-in image service; turret generation was rejected twice, so its depth is an authored smooth approximation. Matching normals are derived by the tracked preparation code. Both materials retain five unique crops, original pivots and mirrored reuse. Marker offsets now use metric depth separately from signed draw-order layers, including turret sockets, PDCs, tube launches, nozzles and bay references. Mirrored frames swap physical lateral identities while retaining source coordinates.

The WebGL gallery also rendered all eight assembled bearings in the in-app Chromium browser. The inspected capture is `build/cruiser-browser.png`. This is graphical evidence; the automated content checks below do not substitute for it.

## Combat journey

The first recorded headless journey cleared waves at ticks 1925, 4236 and 6645, after the ten-second preparation and thirty-second inter-wave delays. All nine receipt-tracked raiders were destroyed, and the main scenario finished with `victory`. Six Tactical craft orders were accepted. A save at tick 600 and forty further ticks matched uninterrupted execution exactly after the launch-transition reference fix.

The initial recovery comparison exposed a stale Flecs component reference after activating a dormant craft. The runtime now reacquires its guidance state after the table transition; synthetic serial-launch coverage uses independent fixtures. An earlier failed attempt remains diagnostic history, not a passing receipt.

The final fit's sustained combat and withdrawal checks remain in progress. An intermediate graphical battle stopped with a Windows hang termination while loading a recovery World; sustained runs now use the headless mode, with separate saved-state graphical captures.

## Transport and integration checks

The Cruiser reused the existing rescue operation with a casualty 350 metres away. Comms opened Dispatch, Engineering supplied a repair team, Tactical selected a real utility shuttle and Science lowered shields. Shuttle delivery and return completed at tick 647 with the repair team recovered. A separate World used qualified Science displacement and completed recovery at tick 364. The local receipt is `build/cruiser-transport.json`.

These exercises exposed an auxiliary-only primary Comms definition, missing craft weapon/magazine feeds, an unsuitable utility transit altitude and a carrier-centre recovery approach. The fit now has a primary Comms Console, complete craft feeds and a suitable utility altitude. The shared recovery fix brakes above the hull; an independent synthetic large-carrier fixture verifies safe recovery. The production craft policies match the completed carried-craft implementation's authored action trees and have no dependency on the development pack.

All 31 native system cases passed. All ten network-service cases passed separately after restoring the missing pinned SDK Node/npm installation. The WASM build check, synthetic content check and production launch preflight passed; the complete final browser gate is still running.

## Limits

Medical was explicitly excluded. No Medical teams, treatment or Medical away contribution is claimed. Physical six-display performance, touch/gamepad and screen-reader operation have not been tested by this exercise. Illustrations and depth drafts approximate geometry; they do not establish exact three-dimensional hull dimensions between views.
