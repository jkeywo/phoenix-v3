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

Setup: Windows native, one 1280 x 720 window/framebuffer, NVIDIA GeForce RTX 5090 Laptop GPU, the built training ZIP, scenario `training.lighting`, slot `training.player`, ship `training.survey`, local Console `main`. This is a functional UI capture, not a reference-machine performance measurement.

```powershell
./build/native/Release/native.exe --local-only --game-pack build/native/training.zip --data build/menu-smoke-save-receipt --menu-smoke
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

The explicitly invoked native `--menu-smoke` journey passed with fresh data, selecting the default Viewscreen in the lobby, checking the sole Settings navigation action, saving from Settings, closing back into the game and quitting to an inactive Main Menu session (`build/viewscreen-menu-smoke.log`). A separate `training.navigation` capture shows the beacon ahead and the world-locked grid from the new POV (`build/viewscreen-forward-native.png`). Native's single-screen lobby also offers Player console or GM inspector as its launch role; configured mixed-display layouts retain their roles.

The manually invoked production-loader comic probe passed its existing depth/shadow/engine/navigation checks and a new synthetic rear-sprite check: relief behind the eye must contribute no foreground pixels while the forward sprite remains visible (`build/viewscreen-comic.log`, `build/comic-forward-pov-smoke.png`). This caught and fixed a positive-only relief denominator that folded behind-camera geometry into the forward view.

The actual in-app WebGL browser journey hosted `training.navigation`, readied through the countdown, rendered the forward world with only Settings, opened Settings and quit to the HTML main menu. Final browser and native views were inspected after rebuilding the corrected projection. These are functional checks on the available machine, not physical reader/device or six-display performance evidence.
