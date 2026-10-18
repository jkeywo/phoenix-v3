# HTML menu and WASM entry validation

18 October 2026, Windows, embedded Chromium browser at 1280 x 720, WebGL 2. Local static server on loopback port 8094, private production signalling service on 8095, verified training pack and `training.lighting` / `training.player` / `training.survey`. This is manually triggered functional evidence, separate from automatic checks and physical reader/controller/phone acceptance.

## Automatic checks

`build/html-menu-check.log` records a passing complete `scripts/check.ps1` gate: native build and twelve system cases, production training verification, WASM cases, native/WASM deterministic trace comparison and all seven real-transport journeys. `build/html-menu-final-network.log` repeats the transport gate after adding a check that late approval controls remain available and actionable through the running lobby. Crew and GM participants in that synthetic journey use the lightweight protocol composition without host simulation services.

`build/html-menu-shell-tests.log` records five passing Node cases for landing/direct-role routing, argument forwarding, unsafe/conflicting parameter rejection, shared sidecar borders, bounded preview data and stored/deflated ZIP extraction/decompression limits. The content integration case checks that public setup hints are generated from its production-resolved synthetic catalogue. Final native and all three browser-role builds are recorded in `build/html-menu-final-native.log` and `build/html-menu-final-web.log`. Formatting/whitespace receipts are `build/html-menu-format-check.log` and `build/html-menu-diff-check.log`.

## Graphical journey

- Inspected the default HTML title, ordered buttons, matching nine-slice art, generated cog/background and four-column Host/scenario/ship cascade. At ship selection, the page's script list contained only `launcher.mjs`; no role bundle had loaded.
- Settings changed volume to 25%, enabled expanded pseudo-localisation, trapped focus, closed on Escape and restored Settings focus. A fresh page load retained both preferences. Normal text/50% were restored for the default capture.
- Closing Settings retained a typed Join Code and its selected join role. The Mod Packs popup showed the installed training pack. Runtime Settings also opened safely from Workshop without a game session.
- Selecting a ship downloaded the exact training ZIP and loaded `server.js` into a stopped C++ lobby. The selected scenario/slot/ship, service and profile were forwarded correctly.
- A direct QR-style `type=client&code=...&service=...` URL bypassed the menu and loaded only `client.js`. Host approval, keyboard Console selection, individual readiness, countdown and the running permitted Console were observed over the production transport.
- A direct `type=workshop` URL loaded the actual ImGui Workshop. A GM invitation loaded the Workshop build in GM mode, requested host approval and opened a read-only inspector with the recovered world's committed tick after ship selection. Running-lobby approval retained its control after activation.
- Save Game created a managed portable save in WASM. Return to Menu completed the browser-storage copy; the HTML Saved Games list showed the new entry. Selecting it opened the stopped recovery lobby, then resumed through readiness/countdown from the saved tick rather than rebuilding starting conditions.
- The runtime return control occupies its own toolbar, leaving game/tool tabs unobscured. Public shell files, standalone art/font/translations and the generated content catalogue are included in the deployment staging list.

Local captures: `build/html-menu-{home,ships,settings,packs,lobby,recovery,workshop}.png`. Large captures remain in ignored build output. No public deployment, physical camera scan, actual screen-reader session, controller device or six-display performance claim is made by this receipt.
