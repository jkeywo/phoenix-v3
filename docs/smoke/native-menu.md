# Native menu and lobby validation

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
