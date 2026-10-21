# Native menu and lobby validation

## Skip sole setup choices

18 October 2026: native and browser New Game automatically select a sole scenario and skip ship selection when it has one slot with one allowed ship. Multiple alternatives remain explicit; Back omits a skipped scenario level. Lobby readiness/countdown is unchanged.

All six launcher checks passed, including independent zero/one/multiple scenario, slot and ship cases. Native presentation and all three WASM roles built; formatting and whitespace checks passed. A running native application held its audio assets open, so the updated native executable is `build/native/Release/native-menu-shortcuts.exe`, linked against the existing unchanged assets with the post-build copy disabled.

Explicit native `--menu-smoke` journeys passed for a fresh synthetic one-scenario/one-slot/one-ship library and a fresh base-game library with multiple scenarios. The former skipped both choice levels; the latter retained scenario selection and skipped its sole ship choice. Both reached the stopped lobby, completed ordinary readiness/countdown, saved from Settings and quit. Synthetic content was packaged from the independent loader fixture under ignored build output. Logs: `build/menu-single-choice-{smoke,base-smoke}.log`. No browser graphical interaction or physical-device acceptance is claimed for this change.

## Default storage correction

18 October 2026: reproduced a desktop-launched native startup failing to create the relative `userdata/menu/packs` directory. Native now locates `%LOCALAPPDATA%\PhoenixV3` through Windows' known-folder API before creating its lobby/library; explicit `--data` paths are retained. Native Release and WASM Workshop builds, `system.native-menu-library` (including different-launch-directory storage identity), formatting and whitespace checks passed. The final desktop launch was stopped by the user before capture; no successful visual check is claimed for this correction.

Date: 18 October 2026. Implementation revision: `653ff0c`.

## Automatic checks

`./scripts/check.ps1` passed all twelve native integration cases, production-content verification, headless WASM cases and the native/WASM deterministic continuation comparison. All seven signalling/real-transport journeys passed, including the new pre-game lobby case.

The synthetic lobby journey checks approval, competing slot claims, automatic verified setup transfer, individual crew readiness, countdown cancellation, fixed-roster activation, mandatory AI ships without extra voters, optional-slot omission, GM/Console admission and portable recovery. It also exercises late crew, rejects using a GM handoff credential as crew, and checks native-form discovery of a direct running ship server without an extra approval request. Existing returning-member, checkpoint, disconnected-operator replacement and verified WSS journeys remain green.

Native and all three browser role targets build. Formatting and staged whitespace checks passed. Local automatic evidence is retained in `build/menu-release-check-verified.log`, `build/menu-delivery-network.log`, `build/menu-delivery-native.log`, `build/menu-delivery-web.log` and `build/menu-format-check.log`.

## Explicit native smoke

Setup: Windows native, one 1280 x 720 window/framebuffer, NVIDIA GeForce RTX 5090 Laptop GPU, the built training ZIP, scenario `test.lighting`, slot `test.player`, ship `base.courier`, local Console `main`. This is a functional UI capture, not a reference-machine performance measurement.

```powershell
./build/native/Release/native.exe --local-only --game-pack build/native/test.zip --data build/menu-smoke-save-receipt --menu-smoke
```

The journey passed both a fresh-data launch and a repeat launch using the same data folder. It recorded:

- The default title, six ordered menu buttons and corner controls, plus the cascading host/scenario/ship columns.
- Settings at 25% volume, installed English and expanded pseudo-localisation, retained across launches.
- A populated pack popup, disable/re-enable/apply actions, keyboard traversal and rejection of background semantic actions while the dialog is open.
- Actual Windows UIA slider range actions and labelled, placed lobby text readouts without Invoke patterns, alongside programmatically triggered RmlUi Tab navigation.
- A stopped lobby, five-second countdown and entry into the selected local Console after its claim succeeds.
- A managed save queued in the lobby and completed once at a fixed-tick boundary while Console presentation remained active; the repeat launch retained the earlier save and added another.

Logs: `build/menu-delivery-smoke.log` and `build/menu-repeat-smoke.log`. Captures: `build/menu-native-home-standard.png` (default menu), and `build/menu-native-{home,settings,packs,host,scenarios,ships,lobby,countdown,running}.png`. The repeated journey uses pseudo-localisation for expansion checks. Large capture files remain in ignored build output rather than source-controlled fixtures.

The existing `--platform-check` native UIA label/bounds/focus/invoke queue probe also passed; its log is `build/menu-uia-check.log`. These programmatic provider and keyboard checks do not establish actual Narrator/NVDA usability, physical touch/gamepad operation, six-display performance or public-network/phone acceptance.

## Comic HUD artwork update

18 October 2026: promoted the existing raw button/backing and their sidecars into `content/ui/menu/`, generated a matching settings cog and bridge background with built-in image_gen, and integrated sidecar-driven RmlUi nine-patch rendering. Prompts are retained in `content/ui/menu/generation.json`.

Native and all three WASM role builds passed (`build/menu-art-native.log`, `build/menu-art-web.log`). All twelve native system cases passed, including synthetic asymmetric nine-slice marker resizing, compressed corners and invalid metadata/resource-path rejection (`build/menu-art-tests.log`). Formatting and whitespace checks passed (`build/menu-art-format-check.log`, `build/menu-art-diff-check.log`). No simulation or transport behavior changed in this artwork increment.

Explicitly ran the same native graphical journey with fresh `--data build/menu-art-smoke`; it passed (`build/menu-art-smoke.log`). Visually inspected the default menu, four cascading columns, expanded labels, Settings and Mod Packs popups, and countdown captures. Corners retain their size, labels remain live, the generated gear is visible at the top left, and the background fills the window without aspect distortion. Keyboard focus has a warm border/tint; modal background controls are dimmed and unavailable. The journey also retained its UIA range/readout, readiness and managed-save checks. Captures for this artwork pass are retained separately as `build/menu-art-{home,settings,packs,ships,countdown}.png`. Setup remains the 1280 x 720 Windows native configuration described above; physical readers/controllers and six-display performance were not exercised.

## Forward Viewscreen and game-state separation

18 October 2026: supersedes the earlier in-game lobby/Console navigation in this receipt. The fixed camera follows own-ship position and forward heading from slightly above the operating plane. Own-ship geometry is excluded. The world fills the display; only Settings remains over it. Browser's separate HTML Return to Menu toolbar has also been removed.

The complete automatic gate passed: all twelve native integration cases, production verification, WASM checks, matching native/WASM continuation and all seven real-network journeys (`build/viewscreen-final-gate.log`). The extended synthetic session test rejects stale navigation/role changes, preserves the current display through Settings and admits explicit quit into a fresh session.

The explicitly invoked native `--menu-smoke` journey passed with fresh data, selecting the default Viewscreen in the lobby, checking the sole Settings navigation action, saving from Settings, closing back into the game and quitting to an inactive Main Menu session (`build/viewscreen-menu-smoke.log`). A separate `test.navigation` capture shows the beacon ahead and the world-locked grid from the new POV (`build/viewscreen-forward-native.png`). Native's single-screen lobby also offers Player console or GM inspector as its launch role; configured mixed-display layouts retain their roles.

The manually invoked production-loader comic probe passed its existing depth/shadow/engine/navigation checks and a new synthetic rear-sprite check: relief behind the eye must contribute no foreground pixels while the forward sprite remains visible (`build/viewscreen-comic.log`, `build/comic-forward-pov-smoke.png`). This caught and fixed a positive-only relief denominator that folded behind-camera geometry into the forward view.

The actual in-app WebGL browser journey hosted `test.navigation`, readied through the countdown, rendered the forward world with only Settings, opened Settings and quit to the HTML main menu. Final browser and native views were inspected after rebuilding the corrected projection. These are functional checks on the available machine, not physical reader/device or six-display performance evidence.

## Lobby invitations, players and local screens

18 October 2026: the host now stays on the selected ship's Viewscreen. The lobby replaces its ambiguous ship/Console and display-role buttons with a Console roster, named claimants, readiness, and native monitor assignments. New games use six-character uppercase alphanumeric Join Codes and a player-join QR. Player profiles include an editable name and an uploaded avatar. Local native Console screens have separate profiles and readiness; the GM inspector can occupy another monitor.

The synthetic real-network lobby journey passed with profile/avatar exchange, local Console reservation and movement, independent local player readiness, runtime ownership and release for late crew (`build/lobby-roster-network-local.log`). Native menu-library, input/semantics and file-import cases passed (`build/lobby-roster-unit-checks.log`). Seven browser launcher checks passed, including short-code routing and profile persistence without requiring an unfetched published pack archive. Native and all three WASM roles build.

The explicitly invoked `--lobby-displays-smoke` journey passed on three detected monitors, spanning a 5760 by 1200 framebuffer: player Console left, host Viewscreen centre, GM inspector right. It exercises separate local readiness, Settings, a committed managed save, return to the Viewscreen and Quit to Main Menu. The capture is `build/menu-native-running.png`; the final journey log is `build/lobby-roster-screen-smoke-layout.log`. The lobby QR was independently decoded with ZXing-C++ to the expected player role, short code and service URL.

An actual in-app browser journey used the local signalling service. It entered a name, uploaded the synthetic pixel avatar through the file chooser, reloaded and confirmed both persisted. A second browser tab joined using a lowercase short code, changed its name, received host approval, claimed the listed Console and readied up. The host showed its claimant and Ready status, then launched into the Viewscreen while the player rendered the claimed Console. `build/lobby-browser-final.jpg` records the browser lobby. This does not establish physical phone-camera, screen-reader, touch/gamepad or six-display performance acceptance.

## Base game and training mod separation

18 October 2026: `base.zip` and `test.zip` are distinct production-verified packs. Base alone has Free Flight; training depends on base and adds two authored practice scenarios, ship variants and patrol AI. Fresh libraries install both and enable only base. All packs can be toggled off; dependency toggles enable prerequisites and disable dependants. This authored smoke content remains separate from synthetic integration fixtures.

The native menu-library integration passed with independent synthetic base/mod fixtures, actual dependency resolution, all-off persistence, reordered prerequisite activation and migration of an edited combined pack without losing a user-authored map or edited practice map. Eight browser launcher checks passed. Production verification passed for base alone and base followed by training; training alone was rejected for its missing dependency. Native and all three WASM role targets build (`build/training-pack-final-content.log`, `build/training-pack-web.log`).

The explicitly invoked native `--menu-smoke` passed using fresh `--data build/training-pack-menu-smoke`. Its pack popup disabled/re-enabled base, enabled the separate training mod, applied the selection and hosted a training scenario through readiness, Settings, save and quit (`build/training-pack-menu-smoke.log`).

An actual in-app browser journey confirmed separate base/training controls, both disabled after reload, automatic base activation when enabling training, and all three scenarios when both are active (`build/training-pack-menu-browser.jpg`). A second journey reused the older browser library on the prior local test origin, disabled training, hosted the sole Free Flight scenario, completed the C++ archive migration and returned to the menu with training still disabled. Existing name/avatar preferences survived. This verifies local browser persistence/migration, not public hosting or physical-device acceptance.

## Native launch, Ready and loading regression — 21 October 2026

The reported blank Viewscreen/Helm exposed two restore failures. Scenario continuation incorrectly required a tree Controller for the World-owned Comms/sensor event source. After correcting that contract, the graphical journey found a second failure: Comms queried spatial rows for streamed asteroids that restore had already deleted. Restore now recognises the explicit event source while still rejecting missing Controllers, reconstructs physical fields and contact indexes before observation-dependent recovery, and skips Controller reconstruction for field bodies.

Console scroll events previously fell through to button actions; the mouse adapter also sent a zero-delta wheel event every frame. Actions now require a click and zero-delta wheel input is omitted. Session preparation and activation yield visible loading frames before synchronous work, and activation publishes its backend only after successful restoration.

The native build and all 28 CTest cases passed (`build/launch-fix-verified-build.log`, `build/launch-fix-verified-ctest.log`). Synthetic production-loader coverage includes initial and continuing event-source recovery, refusal of missing Controllers, Comms recovery with physical streamed asteroids, and deferred session preparation. The explicitly invoked `native.exe --local-only --display-transition-smoke --data build/launch-fix-smoke-data2` passed (`build/launch-fix-smoke-verified.log`): real RmlUi hover/zero-wheel/nonzero-wheel input leaves Ready unchanged, each click toggles once, the two-region game launches, and routed throttle/heading input commits while the host renders its Viewscreen. The inspected captures are `build/menu-native-preparing.png`, `build/menu-native-starting.png` and `build/menu-native-helm-active.png`. These use a 1600 × 900 virtual two-region layout and the bundled Asteroid duel/Armed Courier; they do not establish physical-monitor, touch, gamepad, reader or six-screen performance acceptance. WebAssembly was not rerun for this native defect report.

After the final readiness/disconnect ordering change, the isolated real-transport lobby journey passed (`build/launch-fix-final-network-build.log`, `build/launch-fix-network-isolated.log`), including admission, countdown cancellation, activation and portable recovery. An earlier repeat running alongside graphics timed out during later crew approval (`build/launch-fix-network-final.log`); an earlier run also passed. The isolated pass does not erase that intermittent timeout. Native `--build-check` and the repository formatting check passed (`build/launch-fix-build-check.log`, `build/launch-fix-format.log`).
