# Simplification audit verification � 20 October 2026

The integer readers now validate signedness, representability and bounds before conversion. Timed object-state trees have been removed in favour of the shared hierarchical Controller evaluator. Maintained patrols and synthetic fixtures use once-per-state running actions and elapsed transitions. World and portable saves require schema 7; older saves are refused without deleting their files. Menu avatars and QR images now use RmlUi image decorators and presentation-private in-memory sources.

## Automated verification

- Native build and all 21 system integration cases passed. The added numeric cases exercise the production pack loader, and the cyclic Controller case covers one-tick transitions, exact continuation, restart after handover and transactional refusal of schema 6.
- `scripts/check.ps1 -Target web` passed, including every configured WASM case and exact comparison with the native synthetic continuation trace.
- Both base and training packs verified; no shipped tuning values were frozen.
- The fleet probe was migrated to explicit Controller/channel/generation submissions after the network gate exposed its obsolete action-only requests.
- The final `scripts/check-network-service.ps1` run passed all eight network cases, including real fleet recovery, playable fleet routing, live sessions, verified WSS and lobby admission.

Local logs are under `build/audit-*.log`; they are build artifacts, not checked-in evidence snapshots.

## Manually triggered graphics

The standard native `--menu-smoke` journey passed using an isolated library at `build/audit-menu-final`. It changes the selected avatar twice, traverses Settings and pack/scenario selection, checks keyboard/UIA contracts, readies the lobby, saves and quits. The machine reported a 1920 � 1080 display, a 1280 � 720 application viewport and an NVIDIA GeForce RTX 5090 Laptop GPU with OpenGL 3.3.

The resulting `build/menu-native-scenarios.png` and `build/menu-native-lobby.png` were visually inspected: the newly selected portrait and invitation QR fit inside their CRT frames. Texture-load diagnostics caught an initial source-path normalization error; private URI identifiers fixed it, and the final standard smoke log contains no failed private-image loads. The startup home capture was black and was not used as visual evidence.

The additional `--lobby-regions-smoke` reached the local-profile and assigned-console captures, then stopped at `lobby.profile/test.player/main/automatic`. It is **not a passed multi-region journey**. The harness selects that action from session controls, while the assigned Console exposes its own prefixed complexity controls. Resolving that separate smoke-routing issue, actual screen-reader/device operation and browser graphical image inspection remain outside the evidence established here.
