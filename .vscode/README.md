# VS Code launch and build

On Windows, install Visual Studio 2022 with the C++ desktop workload and the recommended Microsoft C/C++ extension. Run `./scripts/bootstrap.ps1` once as described in [Building Phoenix V3](../docs/building.md). The build and serve tasks use the built-in Windows PowerShell by its full path, so they do not require `pwsh` on VS Code's PATH. The separate formatting scripts require PowerShell 7.

Choose a configuration in **Run and Debug**, then press **F5** (or **Ctrl+F5** to run without debugging):

- **Phoenix: build and run native** configures and builds the `native` application and its dependencies, then runs `build/native/Release/native.exe` from the repository root. This uses the existing Release build; source-level debugging is limited by optimisation and missing debug symbols. Add native command-line options to `args` in `launch.json` when needed.
- **Phoenix: build all WASM and launch web** builds the four launcher runtimes (`server`, `client`, `workshop` and the disposable `phoenix_workshop_test` host) with development web linking, starts a static server on `http://127.0.0.1:8080/`, waits for it to listen, and opens Edge. Use the page's role selector to switch applications. Change the launch type from `msedge` to `chrome` to use Chrome.

**Ctrl+Shift+B** builds native. **Tasks: Run Task** also offers **Phoenix: build all WASM** and **Phoenix: serve web**; the latter builds first and serves without opening a browser. The server uses the bootstrapped Emscripten Python, falling back to `python` on PATH.

These development tasks skip unrelated test/probe executables. The WASM task explicitly enables `-DevelopmentLink`, so it uses fast `-O0` executable linking even after a verification run has restored ordinary linking; C++ compilation stays optimised. It uses a PowerShell command expression to pass the four target names as an actual array. The [World compilation dependency fixes](../docs/world-build-dependencies.md) apply automatically. For complete verification and ordinary Release linking, use `./scripts/check.ps1 -Target native` or `./scripts/check.ps1 -Target web`. See [faster build iteration](../docs/build-performance.md).

Verified on 28 October 2026: both build task invocations passed Windows PowerShell argument-binding and failure-propagation checks using a capture script in a workspace path containing spaces. Launch/pre-launch and serve/build task references also passed. This checks task wiring without recompiling the game or launching the debugger/browser.

Every native build stages the current packs, UI and audio beside that executable, including when only content changed and no relink was needed. Use the named Phoenix launch configuration so its build task runs first. If a build fails, fix the error before launching; choosing to debug anyway runs the previous executable. Stop the running native game before rebuilding so Windows can replace it.

Verified on 25 October 2026: an incremental `native` build replaced a deliberately stale generated runtime locale file with the current asset while the executable's modification time stayed unchanged. The native build check also passed. This checks staging independently of a successful relink.

The server stays running after the browser debug session closes. Stop it with **Ctrl+C** in its dedicated task terminal or **Tasks: Terminate Task**. Stop it before launching again when you want a fresh build. Port 8080 must be free. For a different port, pass `-Port` to the server task and update the launch URL together.

This is a localhost static development server. For multiplayer, start the separate signalling service described in [Network play](../docs/network-play.md).

Configuration fields follow the VS Code [C++ launch reference](https://code.visualstudio.com/docs/cpp/launch-json-reference) and [background task guidance](https://code.visualstudio.com/docs/debugtest/tasks).
