# Opening crawl

22 October 2026, Linux/Xvfb and headless Chromium with software rendering. These are explicitly invoked graphical smoke checks, separate from automated integration tests.

Both menus render the shared introduction in Comic Neue above the panorama/debris and behind the cockpit frame. Inspected entry, middle, arrival and settled captures show the prose receding upward and the existing logo finishing at the top. The English layout uses the 56-second prose baseline and four-second title arrival. Native and browser controls remain available throughout.

The native `--local-only --menu-intro-smoke --data build/crawl-native-smoke-data` journey passed. It verified immediate menu access, natural completion, the static reading dialog's complete final sentence through menu readouts, and closing without replay. Captures are in ignored `build/menu-native-intro-{entry,middle,arrival,settled,reading}.png`; the explicit journey has a 150-second timeout to include reading-dialog checks after the full crawl.

The Chromium journey used the real launcher and production mod verifier, with animation timestamps advanced for the visual checkpoints. It passed at 1280 × 720, 960 × 560, rotated 390 × 844, and 2560 × 1080. It checked that menu branch navigation preserves progress, completion settles the logo, Skip intro restores focus to Read introduction, the reading dialog ends with “against the Stars.”, dialogs complete playback, and the browser's reduced-motion preference settles immediately. No uncaught JavaScript exceptions were reported. Captures are in ignored `build/web/crawl-smoke/`.

All 24 launcher integration cases passed, including hidden-window pause, resize without resetting elapsed time, skip/reduced-motion completion without replay, and measured long-layout pacing. The native menu-library CTest and native `--build-check` passed. The Linux game and all three WASM role targets plus the launch verifier built successfully. Formatting and whitespace checks passed.

These captures establish software-rendered appearance and the exercised interactions. Physical touch/gamepad devices and native screen readers were not exercised; the existing Linux accessibility-provider limitation remains documented in the build instructions.
