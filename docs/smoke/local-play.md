# Local play smoke record

17 October 2026, Windows development machine. Manually triggered functional captures; no performance or physical-device claim.

- Built all four application entry points. Four native system integration cases passed; WASM content, motion/state-tree and local-session checks passed, with matching native/WASM controlled continuation.
- Native build produced `build/native/training.zip` through the production pack writer, loader and verifier. Starter data is separate from synthetic fixtures.
- Served `build/web` on loopback. In server WASM at a 1920×1080 browser viewport, clicked the graphical Add ZIP pack control, selected the training ZIP, inspected mission/slot/allowed-ship choices, and started the session.
- Observed the beacon, player ship and moving traffic in the elevated billboard viewscreen. Opened the authored console; radar, engine and heading widgets occupied their authored regions.
- Increased requested speed and changed heading. The live status showed actual position advancing and heading reaching five degrees; further speed commands produced actual motion. Radar's read action exposed two contacts with IDs, range and bearing in the browser accessibility tree. Browser warning/error log was empty after the journey.
- Captured native console and viewscreen with the documented `--game-pack … --mission … --slot … --ship … --smoke` commands. Both exited normally and released graphics resources. Captures/logs remain under ignored `build/native/Release/`.

The visual pass caught and corrected percent-height resolution in the authored console and CSS/render-size disagreement in the browser canvas. DOM semantic nodes are now retained when labels change, preserving browser focus across a radar report update. This is a programmatic accessibility-tree observation, not a screen-reader acceptance result.

Portable recovery extension, same date: server WASM exported `phoenix-session.zip` while the ship had an admitted speed request and five-degree heading. Reloading that ZIP entered a recovery lobby at tick 387 with a separate reclaim action; clicking it resumed the authored console at tick 391, position 8 m east, actual speed 1 m/s and heading five degrees. Browser warning/error log was empty. Native also loaded the actual browser-exported ZIP without an installed pack and reported saved tick 387, paused. A second manually invoked native run with `--resume-local --console --smoke` reclaimed the console and captured tick 388 with the same position, speed and heading. The browser/native original pack identity travelled inside the save. Native logs and captures remain in ignored `build/native/Release/`.

Still unproven: independent multi-console presentation, five physical controllers, touch devices, complex-script shaping/IME, real screen readers and six-screen 60 Hz performance. Remote clients/GM, QR, peer membership/consensus, forced relay, transfers and fleet recovery are not present in this local journey.
