# Cruiser fit evidence

Explicit authored-content exercises on Windows, 21–22 October 2026. These runs describe this fit and build; they are not production balance assertions in CTest.

## Materials and registration

The native `phoenix_cruiser_smoke` gallery loaded the packaged production Cruiser and rendered all eight assembled bearings at 1600×900. Four captures changed the light direction and turret headings, including ventral inversion. The calibrated pack passed production resolution. Local captures are `build/cruiser-bearings.png` and `build/cruiser-lighting-{port,aft,starboard}.png`.

The hull depth draft came from the built-in image service; turret generation was rejected twice, so its depth is an authored smooth approximation. Matching normals are derived by the tracked preparation code. Both materials retain five unique crops, original pivots and mirrored reuse. Marker offsets now use metric depth separately from signed draw-order layers, including turret sockets, PDCs, tube launches, nozzles and bay references. Mirrored frames swap physical lateral identities while retaining source coordinates.

Final albedos retain every original alpha value (maximum alpha difference zero); the verified archive is 26,510,582 bytes. Annotated marker views are reproducible with `scripts/preview-cruiser-markers.py`.

A saved combat World produced the actual powered engine pulse: forward thrust 1000 at tick 621. Eight-bearing nozzle/occlusion captures are `build/cruiser-engines.png`; the live combat snapshot is `build/cruiser-combat.png`. Gallery heading offsets test articulated rendering separately from the actual combat pose.

The WebGL gallery also rendered all eight assembled bearings with actual powered engine effects in the in-app Chromium browser. The inspected capture is `build/cruiser-browser.png`. A separate browser host reached a running Cruiser session and the Command stance surface (`build/cruiser-browser-command.png`). Its hosted signalling connection failed, so this does not establish remote multiplayer. These are graphical observations; automated content checks do not substitute for them.

## Combat journey

The completed Medical/Security fit cleared waves at ticks 1671, 3892 and 6115, after the ten-second preparation and thirty-second inter-wave delays. All nine receipt-tracked raiders were destroyed, and the scenario finished with `victory`. All eight craft docked before successful FTL departure at tick 8001, with nobody left behind. One interceptor retained 6% condition; finite bay ammunition servicing did not heal it. Six Tactical combat sorties were ordered. The battle produced no onboard clinical orders; the clinical exercise below verifies actual treatment separately.

A save at tick 600 and forty further ticks matched uninterrupted execution exactly. Independent partial-wave withdrawal departed at tick 2401, with wave one complete, wave two active, wave three pending and nobody left behind. Receipts are `build/cruiser-complete-receipt.json` and `build/cruiser-withdrawal.json`. The exact archived pack is `build/native/cruiser-complete.zip`; its SHA-256 is `52e88a5e4bd4dd293a78a08424c58218e71f69142cb0fc640dacdb8220dc0e1f`.

The initial recovery comparison exposed a stale Flecs component reference after activating a dormant craft. The runtime now reacquires its guidance state after the table transition; synthetic serial-launch coverage uses independent fixtures. An earlier failed attempt remains diagnostic history, not a passing receipt.

Sustained combat runs used headless mode, with separate saved-state graphical captures. No between-wave reset or resupply was injected.

## Transport and integration checks

The Cruiser reused the existing rescue operation with a casualty 350 metres away. Comms opened Dispatch and selected stabilisation, Engineering supplied a repair team, Tactical selected a utility shuttle, and Science contributed Medical and lowered shields. Actual treatment healed minor casualties and stabilised severe casualties after repairs made the affected spaces usable. Shuttle delivery, treatment and recovery completed at tick 1705; qualified displacement completed the corresponding journey at tick 1385. Teams and accompanying officers recovered through the ordinary manifest. The receipt is `build/cruiser-transport.json`.

These exercises exposed an auxiliary-only primary Comms definition, missing craft weapon/magazine feeds, an unsuitable utility transit altitude and a carrier-centre recovery approach. The fit now has a primary Comms Console, complete craft feeds and a suitable utility altitude. The shared recovery fix brakes above the hull; an independent synthetic large-carrier fixture verifies safe recovery. The production craft policies match the completed carried-craft implementation's authored action trees and have no dependency on the development pack.

After integration, the complete native gate passed all 33 system cases and all ten network-service cases. The complete WASM gate passed, including native/WASM continuation comparisons for Medical, Security, craft, away missions, objectives and the other affected systems. Shared duty capacity and individual-bay boarding admission use independent synthetic fixtures rather than Cruiser tuning assertions. Logs are `build/cruiser-complete-{native,web}-gate.log`.

## Medical and Security follow-up

The clinical exercise privately seeded one minor and one severe onboard casualty. The production Cruiser's two Medical teams completed field treatment, stabilisation, escorted infirmary recovery and injured Security-team treatment by tick 2482. Interrupted clinical execution resumed exactly. The local receipt is `build/cruiser-medical.json`.

The native two-Console journey selected the ordinary Science team/map order, healed the minor casualty and showed Tactical's Security officers and duties. Inspected captures are `build/native/cruiser-departments-ui.png` and `build/native/cruiser-treatment-ui.png`.

The boarding exercise selected a real utility shuttle, Security and Medical through their owning watches. The ordinary hull threshold blocked insertion into the undamaged target. A private damaged-target variant bounded encounter success/loss probabilities, then exercised actual flight, staging, Comms choices, assessment, routes and continuation. Reactor occupation completed at tick 666 and its party/shuttle recovered. A separate bridge seizure completed at tick 606 and exposed the prize in the Cruiser's operational fleet, retaining the authored prize-crew commitment. Both continuations matched exactly. The receipt is `build/cruiser-boarding.json`. Dispatch deliberately reveals the final result after recovery; the exercise uses authoritative facts to decide when to request extraction.

## Limits

Medical and Security were added after the user authorised their integration. The clinical and boarding exercises use disclosed private starting-state/encounter variants; these are functional evidence, not production encounter balance assertions. Physical six-display performance, touch/gamepad and screen-reader operation have not been tested. Browser rendering does not establish a complete live browser station journey. Illustrations and depth drafts approximate geometry; they do not establish exact three-dimensional hull dimensions between views.
