# Menu scenery and CRT validation

18 October 2026, Windows native and embedded Chromium, 1280 by 720. Manually triggered graphical evidence, separate from automated tests.

## Observed behavior

- The fixed cockpit retains a genuinely transparent window over a separate wider space panorama. Native and HTML display the same comic artwork. The backdrop moves slowly while the frame, title, controls and ceiling-mounted television remain still.
- The final CRT has dense loose cable loops, coiled leads and unplugged connectors. Its upper-right display is 264px wide (previously 240px), with a 329.875px browser height. A test avatar uploaded through the real HTML Settings UI and the saved name `Cable Tester` appear inside the screen in both versions.
- HTML exposes the profile name and avatar through accessibility semantics. Closing Settings restores focus and the cascading Join form remains clear of the television. Native smoke traverses popups, expanded pseudo-localised controls, readiness/countdown, in-game Settings, Save and Quit.
- HTML panorama transforms change during ordinary motion. Enabling Reduced Motion immediately pauses the animation while Settings remains open; separate reads retain the exact transform. Reload retains the stopped state. Disabling the option resumes the pan. The CSS also honors the browser's reduced-motion preference.
- The lobby uses the same television sprite, with the long upper stalk cropped to fit the invitation panel. The complete QR code and quiet border appear inside the glass at entry, without clipping the code or covering the Join Code heading. Modules use integer scaling and nearest filtering. A physical camera scan was not performed in this pass.

Captures are retained locally in ignored build output: `build/scenery-native-home.png`, `build/scenery-native-qr.png` and `build/scenery-web-home.png`. Generation prompts, including the requested additional cabling, are retained in `content/ui/menu/scene-generation.json`.

## Checks

The initial native application build passed (`build/scenery-native.log`). Final native presentation compilation passed (`build/scenery-final-presentation.log`). Because a separately opened native application held `native.exe`, the final preview was linked as `native-scenery.exe` from the same project/libraries without interrupting it (`build/scenery-final-preview.log`); its full menu smoke journey passed (`build/scenery-final-smoke.log`). All three final WASM role builds passed (`build/scenery-final-web.log`). The eight existing HTML shell cases passed (`build/scenery-shell-tests.log`).

Changed presentation C++ and menu web/data/document files pass their formatting checks (`build/scenery-cpp-format.log`, `build/scenery-own-format.log`), and `git diff --check` passes. The complete repository formatting check reports drift in other in-progress native input, Console artwork/content and test edits (`build/scenery-format.log`); those changes were kept separate from this menu work. This receipt does not establish physical screen-reader/controller, multi-display performance or public-network acceptance.
