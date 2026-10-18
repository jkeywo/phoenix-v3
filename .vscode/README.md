# VS Code launch and build

On Windows, install Visual Studio 2022 with the C++ desktop workload and the recommended Microsoft C/C++ extension. Run `./scripts/bootstrap.ps1` once as described in [Building Phoenix V3](../docs/building.md). The build and serve tasks use the built-in Windows PowerShell by its full path, so they do not require `pwsh` on VS Code's PATH. The separate formatting scripts require PowerShell 7.

Choose a configuration in **Run and Debug**, then press **F5** (or **Ctrl+F5** to run without debugging):

- **Phoenix: build and run native** configures and builds native targets, then runs `build/native/Release/native.exe` from the repository root. This uses the existing Release build; source-level debugging is limited by optimisation and missing debug symbols. Add native command-line options to `args` in `launch.json` when needed.
- **Phoenix: build all WASM and launch web** configures and builds the complete web target set, including server, client and Workshop, starts a static server on `http://127.0.0.1:8080/`, waits for it to listen, and opens Edge. Use the page's role selector to switch applications. Change the launch type from `msedge` to `chrome` to use Chrome.

**Ctrl+Shift+B** builds native. **Tasks: Run Task** also offers **Phoenix: build all WASM** and **Phoenix: serve web**; the latter builds first and serves without opening a browser. The server uses the bootstrapped Emscripten Python, falling back to `python` on PATH.

The server stays running after the browser debug session closes. Stop it with **Ctrl+C** in its dedicated task terminal or **Tasks: Terminate Task**. Stop it before launching again when you want a fresh build. Port 8080 must be free. For a different port, pass `-Port` to the server task and update the launch URL together.

This is a localhost static development server. For multiplayer, start the separate signalling service described in [Network play](../docs/network-play.md).

Configuration fields follow the VS Code [C++ launch reference](https://code.visualstudio.com/docs/cpp/launch-json-reference) and [background task guidance](https://code.visualstudio.com/docs/debugtest/tasks).
