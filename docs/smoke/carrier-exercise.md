# Carrier exercise UI journey — 21 October 2026

This explicitly invoked graphical smoke run uses `test.carrier.exercise`, `test.carrier.ship` and its authored watch profile. It is excluded from CTest and the automatic gate. It opens two 1280 × 900 Console regions in one 2560 × 900 native window, with Helm and Engineering claimed separately, using Windows raylib/OpenGL on Intel Graphics.

Run from the repository root:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_craft_smoke
./build/native/Release/phoenix_craft_smoke.exe build/native
./build/native/Release/phoenix_craft_smoke.exe build/native ship
```

The runner waits for Console controls after graphics preparation. It uses production UI actions and accessibility focus; manual approach uses the same ConsoleModel throttle and heading bindings as Helm widgets. It advances the real session and checks transport and craft phases, without moving entities or injecting mission outcomes directly.

The shuttle journey opens assignments, contributes Engineering's repair team, selects a named shuttle, sends it, waits for physical delivery and authored release, then assigns another shuttle for recall and docking. It exercises interceptor defence and escort, bomber attack and automatic return, servicing, manual recall and charging-triggered FTL recall. Accessibility focus reaches the bomber action below the fold. The ship journey checks refusal outside deployment range, manually sets heading and throttle, stops alongside, deploys and recovers the party.

The run exposed and fixed:

- Missing Engineering observation ownership, which hid the repair-team assignment choice.
- Helm toolbar overlap and the carrier radar/throttle/panel geometry.
- A full-screen FTL panel shown over Helm whenever it had FTL access. Consoles with authored widgets retain their ordinary equipment panel; dedicated Navigator consoles retain the FTL panel.
- Settings scroll resets during live reports, horizontal clipping of padded rows and off-screen accessibility focus.
- Self-escort and idle docked recall choices, and transport assignment buttons on completed recovered missions.

Both journeys pass. Native player and browser player builds are checked separately. Captures and logs are local artifacts under `build/`: `carrier-helm.png`, `carrier-shuttle-assigned.png`, `carrier-deployed.png`, `carrier-recovered.png`, `carrier-bomber-controls.png`, `carrier-sorties.png`, `carrier-serviced.png`, `carrier-ftl-recall.png`, `carrier-ftl-recovered.png`, `carrier-ship-alongside.png`, `carrier-ship-deployed.png`, `carrier-ship-recovered.png`, `carrier-final-journey.log` and `carrier-final-ship-journey.log`.

This automated graphical route and the inspected captures establish the native journey on this machine. Browser interaction, physical touch/gamepad input, an actual screen reader, multiplayer admission and six-display performance remain separate.

## Shared-system refactor replay

Both native journeys passed again on 21 October 2026 using the isolated `build/craft-review-native` build. The shuttle runner additionally requires the bomber's ammunition to decrease during its sortie before accepting its return. Logs are `build/craft-review-carrier.log` and `build/craft-review-ship.log`; the captures above were refreshed and inspected.

All eight affected native integration cases passed: flight, away missions, craft, FTL, Controller runtime, combat, conversations and missions. They cover authored sortie-policy replacement, continuation with obsolete craft service keys, exact radio-range boundaries and an escort retaining its last reported anchor during observation loss. The replay uses the authored Controller policies with explicit weapon and magazine feeds and an acquisition grace period.

A fresh WASM validation could not run because the local Emscripten installation is missing its Clang executables. Earlier browser build results above describe the preceding implementation, not this refactor.
