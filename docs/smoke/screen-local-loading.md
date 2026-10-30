# Screen-local asynchronous loading — 30 October 2026

The presentation pass now continues through every display while content prepares. Host menus/lobbies keep their controls and show a local progress card. Each Console prepares its ship presentation independently, with display/request-scoped cancel and retry. Session generation changes retain matching resource owners; revoked command authority still forces fresh Console preparation.

Native workers decode shared immutable pixels. Graphics owners retain these pixels for material checks, CPU sampling and later mipmapped uploads; RmlUi shares premultiplied UI textures. Graphics uploads and RmlUi updates remain on the graphics thread. Cancellation stops publication and further preparation on that display without joining a worker or leaving the session. An already running decode can finish and be reused by a matching retry; superseding its content owner discards the result. A registry reaps exited threads and waits for unfinished jobs only at process shutdown. Threaded WASM uses the worker path; ordinary WASM yields between resources.

## Executed native checks

- Release native runtime, input, content, lobby, local-session, menu-library and Console-transition targets build. `native --build-check` passes.
- Five focused CTest cases pass: `system.native-menu-library`, `system.input-and-semantics`, `system.content-and-packs`, `system.local-session`, and `system.lobby-worker-consoles`. Receipt: `build/screen-loading-native-tests.log`.
- The input case covers independent display requests, retained completed content, stale success/error refusal, cancellation, retry and unchanged-identity readiness. Content checks load synthetic archives through production paths, exclude unrelated scenery from Console preparation, reuse immutable decoded pixels, and reject damaged image replacements.
- Explicit `phoenix_console_transition_smoke --loading-only` passes with two independent synthetic ships, one borderless 2560 × 720 window and two 1280 × 720 regions. Commands remain admitted on the ready Console throughout the other display's loading/cancel/retry, and affect only its own ship. The journey checks asynchronous decode failure, discarded failures, display reassignment, unchanged-content generation transitions and revoked controls. Receipt: `build/screen-loading-native-journey.log`; captures: `build/screen-local-loading.png` and `build/screen-local-loading-cancelled.png`.
- Explicit `phoenix_console_transition_smoke build/native --loading-lobby` passes with a 3840 × 2160 host region and two 1920 × 1080 Console regions. The host can ready/unready a Console during asset preparation, then both Consoles activate and graphics close normally. Receipt: `build/screen-loading-production-loading.log`; captures: `build/console-transition-local-loading.png`, `build/console-transition-running.png` and `build/console-transition-helm-1080p.png`.

The native graphical run uses Windows, raylib 6.0 and OpenGL 3.3 on an NVIDIA GeForce RTX 5090 Laptop GPU (driver 617.14). The capture was visually inspected: the first Console remains drawn beside the other display's local progress state. Existing RmlUi stylesheet parser warnings remain in the graphical log; they are not new loading failures.

## Executed browser checks

Threaded WASM `client`, `server` and `workshop` build with the existing development-link configuration (Release C++ compilation). The final link receipt is `build/screen-loading-web-current-build.log`. `phoenix_input_check.js`, `phoenix_content_check.js` and `phoenix_session_check.js` pass under the bundled Node 24.19.0 from `build/web`; their receipt is `build/screen-loading-web-tests.log`. The content and session cases use synthetic fixtures through the production loader and session paths. These are functional checks, not browser rendering or performance evidence.

Changed C++ files pass pinned clang-format checks; changed CMake files pass gersemi checks, and the loading documentation passes Prettier checks. Unrelated working-tree changes are excluded from the feature commit.

## Acceptance boundaries

The synthetic journey and virtual display regions establish rendering, command routing and preparation lifetime behavior. They do not establish six physical displays at 60 Hz, simultaneous physical touch/gamepads, or actual screen-reader usability. Browser graphical interaction and ordinary single-threaded WASM acceptance remain separate from compilation and headless checks.

The older full station journey referenced the retired `base.rescue-courier`; it now uses `base.courier`. Its loading/launch section reached both rendered Consoles, but its later `Compact weapon toggles are visible` assertion does not match the current Courier workload. That broader journey is not recorded as passing. `--loading-lobby` runs its production mixed-resolution loading/activation section independently of those later workload assertions.
