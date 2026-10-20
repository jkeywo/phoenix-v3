# Asset loading and Workshop packaging � 20 October 2026

The implementation supports Workshop **File > Package� > Game pack / Selected scenario > Source ZIP� / Compiled DAT�**, compiled image/definition loading, per-scenario preparation, and restricted client Console-set packages. This receipt records exercised software boundaries, not physical-device, screen-reader or six-display performance acceptance.

## Automatic checks

- All 22 native system integration cases passed in `build/asset-native-final.log`, including source-directory/ZIP/DAT loading, dependent scenario exports, exact pixels, editable reimports, malformed input, restricted Console sets, and declared/undeclared dynamic spawn handling through the production ScenarioRuntime.
- The final WASM gate (`build/asset-web-final.log`) completed successfully, including the native/WASM continuation comparison. HTML launcher checks passed, including bounded DAT metadata preview.
- The isolated real-transport client/reconnection/GM journey and pre-game multi-ship/readiness journey both passed in `build/asset-network-final.log`. These exercise compiled client bundles and additional Consoles supplied by a profile.
- Both authored packs were exported as DAT and resolved together through the production loader. The generated artifacts are `build/native/base.dat` and `build/native/test.dat`. No production tuning values were frozen.
- Browser runtime metadata was inspected: `client.js` contains neither `/assets/base.zip` nor `/assets/test.zip`; Workshop retains its bundled authoring packs. The client launcher also avoids fetching or copying installed gameplay archives into the client filesystem.

## Manual graphical evidence

The native compiled base pack was launched into `base.free-flight` with the engineering Courier. `build/asset-loading-final.png` shows the background, scenario title, graphics phase, filled progress bar and cancellation control. `build/asset-dat-play.png` captures gameplay after preparation completed. Images were visually inspected. This was an explicitly invoked smoke, with no machine-performance threshold asserted.

Workshop exports are covered through its production document/export APIs. Native Save As interaction, browser file-picker interaction, actual screen-reader use and physical input devices were not manually exercised in this pass.

## Remaining integration result

The full transport run in `build/asset-native-final.log` passed six of eight cases and reported two failures: remote lateral-thrust neutral release and a GM inspection timeout. Repeating the client/GM and pre-game journeys without concurrent compiler load passed. The isolated playable-fleet case still fails at **Remote manual release did not admit exact neutral axes** (`build/asset-fleet-final.log`). Concurrent flight changes in `World::observe_at` remove the zero-valued lateral control descriptor used by that contract. Those flight changes were left untouched by this packaging task. This failure is not relabelled as a full-gate pass.
