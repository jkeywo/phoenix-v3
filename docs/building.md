# Building Phoenix V3

## Formatting

Formatting uses Python 3.10+, Node.js 22+ (with npm), and PowerShell 7 on Windows. It is independent of the native/WebAssembly build:

```powershell
./scripts/bootstrap-format.ps1
./scripts/format.ps1
./scripts/format.ps1 -Check
```

Bootstrap installs pinned clang-format, gersemi, Prettier and PSScriptAnalyzer versions into ignored local tool directories. The npm dependency tree is locked in `tools/format/package-lock.json`. Use `-Python <executable>` or `-Npm <executable>` on bootstrap when those tools are not on PATH; the existing Emscripten Python installation is also recognised.

The format command covers tracked and non-ignored untracked authored files, including uncommitted edits. It never stages or commits changes. `-Check` reports drift and fails without editing files; the separate Formatting GitHub Actions job runs that same check without building the game.

C++ and PowerShell use tabs (display width four) and next-line opening braces. Prettier handles JavaScript, HTML, CSS, JSON, YAML and Markdown, using its standard same-line braces for JavaScript/CSS, including scripts and styles inside HTML. YAML and Markdown retain spaces for syntax and portable rendering, and prose wrapping is preserved. CMake uses tabs through gersemi. `.editorconfig` shares the whitespace defaults with editors; configure format-on-save to use the corresponding repository formatter/configuration and pinned version.

The command explicitly selects authored roots and excludes `references/`, dependencies, generated directories, lockfiles and minified files. RML/RCSS dialects are excluded pending formatter compatibility validation. C++ raw string assets are preserved, and Emscripten JavaScript macro bodies are protected from C++ formatting. Do not run a blanket formatter over historical snapshots or build output.

## Game build

First-party C++ uses named modules with `.cppm` interfaces. See [module ownership and conventions](modules.md). CMake 3.28 or newer is required; the pinned bootstrap version remains 3.31.6. The native and web build trees maintain separate compiler-generated module artifacts.

Windows requires Python, Git and Visual Studio 2022 with the C++ desktop workload/Windows SDK. Run from the repository root in PowerShell:

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1 -Target native
./build/native/Release/native.exe --build-check
./.tools/python/cmake/data/bin/ctest.exe --test-dir build/native -C Release --output-on-failure
./scripts/build.ps1 -Target web
python -m http.server 8080 --directory build/web
```

The web build also supports Linux with PowerShell 7, Python and Git. The same bootstrap installs the pinned Linux CMake, Ninja, Node and Emscripten tools. Run `./scripts/check.ps1 -Target native` on Windows to verify the native build and network gate; it writes `build/native/continuation.trace`. After copying that file and the verified `base.zip`, `training.zip` and their menu sidecars into `build/native` on the web build machine, run `./scripts/check.ps1 -Target web`. The web gate runs every WASM system case and compares its continuation with that native trace. Omitting `-Target` retains the complete local Windows gate.

Open `http://localhost:8080/`. The launcher accepts `type=server|client|workshop`, `mode=editor|gm`, `code`, `slot`, `service` and `profile`. Connection parameters are hints and cannot grant authority. Start the signalling service for [network play](network-play.md).

Bootstrap installs CMake 3.31.6, Ninja 1.11.1.3 and Emscripten 6.0.12 into ignored `.tools/`; it does not alter the system installation. Initial configuration downloads dependency source archives. CMake pins each by commit and SHA-256: Flecs 4.1.5, raylib 6.0, RmlUi 6.2, SDL 3.4.16 (native controllers only), ImGui 1.92.0 docking and FreeType 2.13.3. `CMakeLists.txt` contains the immutable revisions and hashes. Fonts retain their upstream licence in the build assets.

Native links the authoritative simulation and tools. Browser server links simulation and presentation; browser client links presentation; browser Workshop links tools and presentation. The libraries are static. A successful build does not establish the pending accessibility, device, gameplay, networking or performance acceptance checks in the implementation plan.

The native cascading menu has an explicitly triggered `--menu-smoke` journey; see [native menu validation](native-menu.md#validation).

For an explicit graphical platform probe, `native.exe --platform-check` initialises the real UIA provider, checks a labelled control's rendered bounds, then focuses and invokes it through the provider's COM interface/production action queue. This is a programmatic contract check, not evidence of actual Narrator/NVDA operation. `--smoke` captures the home framebuffer and exits; add `--workshop` for the tool surface. These graphical checks remain manual.

The bounded automated `system.input-and-semantics` case supplies synthetic presentation configuration directly to the production region validator/router. It verifies independent contact capture, explicit keyboard focus, five logical gamepads, neutral release and semantic placement/admission. It does not exercise a ship/content loader because it tests presentation routing. Native controllers now use SDL rather than raylib's four-device API; physical five-controller operation remains a manual check. See [display configuration and hardware evidence](console-displays.md).

`scripts/check.ps1` runs the automatic build/system gate without graphical smoke runs. The content integration case uses independent files under `tests/fixtures/content` and exercises the real ZIP/JSON/PNG loader in native and WASM, including inheritance, changed tuning, ordered overrides, invalid bindings/references/images and modified-pack identity refusal. The WASM case runs from `build/web` so its fixture bundle resolves correctly. The [content format](content-format.md) documents verification and pack limits. JSON 3.11.3, miniz 3.0.2 and PicoSHA2 1.0.1 are pinned by source revision and archive hash in CMake.

GitHub Actions runs the same non-graphical gate for code/build changes. CMake rejects transitive simulation/Flecs dependencies in browser client or Workshop targets, and graphics/tools dependencies in simulation. Manual smoke captures remain separate from CI.

The gate also runs the gameplay foundation, scenario situation, knowledge/Coordination, paid flight, scenario World, named summary bindings, swept collision and disposable Workshop-host system cases on native and WASM. The web executables are `phoenix_gameplay_tests.js`, `phoenix_scenario_tests.js`, `phoenix_gameplay_knowledge_tests.js`, `phoenix_gameplay_flight_tests.js`, `phoenix_scenario_world_tests.js`, `phoenix_summary_bindings_tests.js`, `phoenix_swept_collision_tests.js` and `phoenix_workshop_test_tests.js`. They execute from `build/web` against preloaded test-only fixtures through production loaders and authoritative paths. Preference coverage checks inherited command reads, localised discrete choices, independent channels reaching ordinary Controller AI, and resumed intent state. The gameplay/scenario source files currently under `tests/manual/` are registered automatic system cases; their directory name does not turn them into graphical smoke evidence. No shipped-ship tuning or physical-device acceptance is established by these cases.

Workshop's disposable browser simulation runs in `workshop-test-worker.js`, which loads the separately compiled `phoenix_workshop_test.js`/`.wasm` host. The Workshop UI itself remains free of authoritative simulation dependencies. Local web builds place all three worker files together in `build/web`; Test transfers the current verified draft and its declared dependency archives to the host. The worker executable contains no preloaded synthetic fixture bundle. A successful worker/system case verifies its authoring isolation and execution contracts; an actual browser Test journey remains manually triggered smoke evidence.

The simulation integration case exercises real Flecs-backed fixed ticks, local admission, range-filtered radar, collision pairs and resumed Console Controller tree work. The gate compares native/WASM continuation within the same synthetic fixture/run, without a stored expected world hash. The [simulation notes](simulation.md) state its present limits. The service gate also exercises real native host/client/GM processes through production admission and projections.

Native builds also produce the `phoenix_pack` source-folder packaging utility, `build/native/base.zip` and a separate `build/native/training.zip` mod, verified in that dependency order. See [local play](local-play.md) for starting an authored mission in native or server WASM. The fourth native system case and its WASM equivalent exercise the local session adapter, transactional setup/import, command ownership, content-inclusive runtime saves and paused recovery with a fresh console claim. These assertions use synthetic content and compare continuation within the same run.

The peer-consensus case runs on native and WASM against synthetic production-loader worlds, including delayed durability, partitions, majority continuation, minority pause and returning-peer catch-up. A sixth native case exercises the actual asynchronous flushed journal. Actual browser IndexedDB durability/reload and exclusive ownership are manually triggered through `storage-probe.html?key=your-test-namespace` in the served web output. Its reload button requires recovery of an existing record. Browser storage checks are not represented by the Node-based WASM gate; see [storage evidence](smoke/peer-storage.md).

Native networking additionally builds pinned libdatachannel 0.24.6 and Mbed TLS 3.6.7 static dependencies (their submodules are pinned by immutable parent commits). Git and Git for Windows' OpenSSL executable are required. The automatic gate installs the integrity-locked `service/` dependency with the bundled Node/npm and exercises signalling, relay and actual native verified WSS. The WSS fixture checks both an explicitly trusted ephemeral CA and refusal without it. Direct native/browser connectivity remains a manually triggered [transport smoke](smoke/network-transport.md); see [network setup](network-transport.md).

## Linux native game and packager

The Linux port builds the same `native`, `phoenix_pack` and `phoenix_verify` targets, plus the native system and network probes. Use Clang 19 with its matching `clang-scan-deps`, CMake 3.28+ (3.31.6 is the repository bootstrap pin), Ninja, Python, Git, Node 22+ and OpenSSL. GCC is not selected by the wrapper because the first-party C++20 named modules need a supported CMake dependency scanner.

On Debian 13 / Ubuntu with Clang 19 packages available, install the development dependencies:

```sh
sudo apt-get update
sudo apt-get install clang-19 clang-tools-19 cmake ninja-build python3 git openssl pkg-config \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libasound2-dev libudev-dev libdbus-1-dev libgtk-3-dev
./scripts/build-linux.sh
./build/linux/native --build-check
./scripts/check-linux.sh
```

Install Node 22+ with npm separately if the distribution packages are older, and confirm `node --version` before running the checks. The cloud environment already provides Node 24.19.0.

Run commands from the checkout root. `./scripts/build-linux.sh native phoenix_pack` selects just those targets; `JOBS=4` controls parallelism. `CC`/`CXX` can select another compatible Clang installation on the first configuration. Dependencies retain the same immutable revisions and archive hashes as Windows/WebAssembly. SDL supplies gamepads only; raylib/GLFW owns the X11/OpenGL window. Wayland desktops need XWayland. GTK 3 supplies file pickers and clipboard services.

The packager is a command-line executable and needs no display:

```sh
./build/linux/phoenix_pack content/base build/linux/base.zip
./build/linux/phoenix_pack content/training build/linux/training.zip build/linux/base.zip
./build/linux/phoenix_verify build/linux/base.zip build/linux/training.zip
./build/linux/native
```

The normal build already generates and verifies both packs and their menu sidecars. Keep the executable beside its generated `assets/` directory. Native user data defaults to `$XDG_DATA_HOME/phoenix-v3` or `$HOME/.local/share/phoenix-v3`; `--data` overrides it. Linux journal ownership uses a separate `flock` lock file so it survives atomic checkpoint replacement. Journal/blob durability uses file and directory `fsync`. Secure tokens use `getrandom`; verified WSS uses the distribution CA bundle, `SSL_CERT_FILE`, or the existing explicit CA option.

`check-linux.sh` runs the registered CTest suite, the native build check, simulation trace, HTML launcher suite and all network-service cases with real Linux probes, including trusted/untrusted WSS. It creates an ephemeral TLS fixture under the ignored build directory and removes it on exit. Graphics remain separately invoked smoke evidence. For a machine without a desktop, install `xvfb` and `xauth`, then run `(cd build/linux && xvfb-run -a ./native --local-only --smoke --data smoke-data)`.

This initial port supports the ordinary mouse/keyboard path and SDL gamepad code. Native Linux AT-SPI screen-reader integration is not implemented. SDL touch contacts now route through the same presentation input path as browser and Windows contacts; physical Linux touch devices still need hardware verification. The accessibility probe returns failure rather than claiming support; the full `--menu-smoke` journey therefore also fails at its accessibility assertion on Linux. Windows UIA and browser DOM semantics are unchanged. Physical gamepads, Linux desktop file-dialog interaction and real multi-display performance still need device testing; a build or virtual-display smoke does not establish that acceptance.

Linux validation on 20 October 2026 used Debian 13, Clang 19.1.7, CMake 3.31.6 and four build workers. The full Linux gate passed 21 CTest cases, 10 launcher tests and all eight service/network cases with no skips, including real fleet recovery and verified WSS. The packager generated both shipped packs and `phoenix_verify` accepted their combined content. Explicit Xvfb/llvmpipe smoke runs rendered the home screen and the `training.navigation` Console and Viewscreen; the profile journey loaded all avatar resources after preserving POSIX absolute paths. That full menu journey still failed its native accessibility assertion, consistent with the unsupported Linux provider. There is no audio device in the cloud machine, so sound output was not verified. Windows/WebAssembly checks were not rerun for this port.

## GitHub Pages

The public launcher is **https://jkeywo.github.io/phoenix-v3/**. Use Host Game, one of the three Join Game roles, or Workshop. The HTML menu offers verified training scenarios and defers WASM until lobby/Workshop entry; QR links bypass the menu. See [browser menu behavior](browser-menu.md).

`.github/workflows/build.yml` builds and deploys on every push to `main`, including documentation changes. It also supports manual dispatch on `main`. Pull requests run the code/build gate and stage the site without publishing it. Native compilation, content verification and native/network system cases run on Windows; WebAssembly compilation and all WASM system cases run on Linux. Both jobs use `scripts/check.ps1` with their respective `-Target`, each retaining a 30-minute limit. A commit-pinned artifact transfer carries this run's verified packs and native continuation trace to the web job. Its exact native/WASM comparison must pass before staging, and deployment depends on both jobs succeeding. Graphical smoke runs remain manually triggered.

The workflow stages `index.html`, `menu.css`, `launcher.mjs`, standalone menu artwork/font/translation assets, each browser role's `.js`, `.wasm` and `.data`, the disposable `workshop-test-worker.js` and `phoenix_workshop_test.js`/`.wasm` host, verified `base.zip` and `training.zip` with their `.menu.json` previews, and `revision.txt` in `build/pages`. The `.data` bundles contain runtime assets and their font/QR licence notices. The worker files are copied explicitly; system-test executables, synthetic fixture bundles, manual probes, build intermediates and local user data are not published. The launcher and worker use relative URLs, so assets resolve beneath the repository's `/phoenix-v3/` path. `revision.txt` identifies the deployed commit.

In repository **Settings → Pages**, the source must be **GitHub Actions**. The deployment job uses the `github-pages` environment and has `pages: write` and `id-token: write`; the build job only needs repository read access. No deployment secret or generated-output branch is required. The actions are commit-pinned. Builds for the same ref are serialised without cancelling a running deployment; if verification fails, the previous published site remains available. A successful manual run on `main` can republish it.

Pages hosts static browser files only. It does not run `service/` or provide signalling, relay or a native game server. Native and browser multiplayer default to the hosted Northflank [network service](network-transport.md). Override it using **Network service** in Advanced Connection Settings, the web `service` query parameter, or native `--service`. For local development, explicitly select `ws://127.0.0.1:9080/phoenix`. Workshop authoring does not require that service. Hosting does not establish the pending mixed-platform, accessibility or physical-display acceptance checks.
