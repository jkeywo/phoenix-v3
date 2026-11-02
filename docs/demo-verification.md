# Demo verification — 31 October 2026

The demo feature implementation is on `codex/base-game-demo`, based on `587c301e`, with isolated source under `build/demo-source`. The initial implementation commit is `7d4d619d`; the follow-up commit records formatting, smoke harness adjustments and this receipt. Isolation keeps concurrent controller/content edits in the shared checkout out of these artifacts.

Windows x64 Release and non-threaded Emscripten Release builds both completed using `cmake/demo.cmake`. All five optional features are OFF. Final dependency graph checks reject Workshop, GM and ImGui links into demo runtimes.

Passed checks:

- `system.build-features`: synthetic pack loading, disabled content/role admission, accessible actions, and explicit save/resume through production paths.
- `node tests/system/feature_network.mjs`: same-ship player joins and host approval through a temporary loopback signalling service.
- `node --test tests/system/web_launcher.mjs`: 40 passing launcher tests.
- `python scripts/check-module-boundaries.py`: 722 C++ sources, 235 module units, zero violations.
- Content-only WASM verifier: actual Phoenix Base Game archive accepted, including its production scenario.
- Native and packaged native `--build-check`: assets load and Flecs advances a tick. Disabled `--workshop`, `--gm` and `--base` preflights reject launch.
- Browser deployment verifier: selected production packs and required client companions present; development roots excluded.
- Both ZIPs: every SHA-256 manifest entry verified; one Phoenix Base Game archive each; no Workshop or Test Missions files. Native includes app-local MSVC runtime DLLs.
- Changed C++, CMake, JavaScript and documentation formatting checked; PowerShell scripts pass the repository formatter.

Native graphical smoke passed: `native.exe --menu-smoke-large --local-only --data <fresh-directory>`. The authored `base.rescue-under-threat` scenario reached ship selection, an AI-crewed host lobby, visible Ready bounds, countdown and gameplay in one 1872�972 Windows window. The manually invoked harness used supported semantic actions and UI/accessibility probes; no physical touch or controller was connected. Final log: `build/isolated-demo-native-final2.log` in the main checkout.

The first isolated diagnostic smoke timed out during content loading under debugger attachment. The normal run subsequently reached gameplay, then found a stale harness assertion that omitted the existing AI configuration action for AI-crewed games. A subsequent precise check also distinguished read-only accessibility text from navigation actions. The final harness permits AI configuration, checks actual navigation actions separately and gives the large authored-content smoke a longer bound; its rerun passed with exit code 0. Earlier shared-directory results were not used as final runtime evidence because concurrent source/ABI changes made them inconsistent.

Interactive browser, physical touch/gamepad, screen-reader, six-display and performance acceptance were not established by these checks. The browser UI launch attempt was unavailable after the app launch approval timed out; WASM verification and launcher integration checks completed headlessly. Nothing was deployed.

Final packages (paths relative to the isolated checkout):

- `build/demo-packages/phoenix-demo-native-20261031-181544.zip`
- `build/demo-packages/phoenix-demo-web-20261031-180528.zip`

Detailed logs remain in the main checkout's ignored `build/isolated-demo-*.log` files.
