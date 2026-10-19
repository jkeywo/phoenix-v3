# Menu and lobby audio evidence

19 October 2026, Windows native and embedded Chromium at 1280 by 720. Explicitly triggered graphical/audio checks are separate from automatic integration checks and physical-device listening/accessibility acceptance.

## Observed

- V2�s `assets/sounds/ui_click.ogg` was copied unchanged to `content/audio/ui_click.ogg`; both SHA-256 values are `8a0784b61aec0de9b5d0973ac8a9fb366cc5c25447de7b3877357ad124cc84c3`. V2 was read only.
- The HTML Settings popup exposes Master, Music and SFX ranges at 50% initially. Keyboard changes survive closing/reopening and reloading. Muting SFX or Master suppresses source starts; restoring their levels restores clicks. A development-only Web Audio wrapper observed playback rates including 1.05383, 1.01410 and 1.01085 and a combined default gain of 0.25. Source duration is about 0.997s. This instruments actual Web Audio sources rather than substituting a fake player.
- The loaded server WASM lobby exposes the same three sliders. With Music set to zero and Master/SFX at 50%, keyboard activation of **Use this profile** produced 25 additional nonzero audio blocks in the actual miniaudio ScriptProcessor output. Observations are retained in `build/click-web-audio-observations.json`. The public signalling connection failed during this local run; this is not a network-admission check.
- The native menu journey changes Master/Music/SFX independently to 60/25/35, checks RmlUi keyboard traversal and Windows UIA range actions, visits the lobby/readiness countdown, saves and quits. The audio device and copied click recording load successfully. `build/click-native-settings.png` captures all three sliders with expanded pseudo-localised labels; `build/click-web-settings.png` captures the final HTML popup with restored 50% settings and no horizontal overflow.
- The final runtime settings path requests IndexedDB persistence immediately after successful local preference writes, alongside the existing periodic/session-exit snapshots. As with other asynchronous browser writes, abrupt process termination can still interrupt an in-flight transaction.

## Automated checks

Native and all three role WASM builds pass. Focused native menu-library and input/semantics integration cases cover new defaults, retained older music settings, independent mixer persistence, numeric bounds, unavailable edits and the shared click activation hook. The WASM input/semantics executable and eight existing HTML shell cases pass. A restricted native rerun failed at Win32 atomic library replacement (`build/click-integration.log`); rerunning with normal Win32 permissions passes both cases (`build/click-integration-final.log`). Native smoke, build, integration and formatting logs are retained under `build/click-*`.

The click pool has eight reusable native/WASM voices; the HTML mixer also bounds active voices at eight. Both apply local 0.94�1.06 pitch variation. Audio state and random streams do not enter session replication or simulation state. Graphical evidence does not establish physical sound quality, actual screen-reader usability or controller-device acceptance.
