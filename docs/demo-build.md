# Demo and independent build features

The demo is a distribution preset, not a separate gameplay mode. Each capability has its own CMake option and matching C++ definition (`0` or `1`):

| Option                         | Default | Demo |
| ------------------------------ | ------- | ---- |
| `PHOENIX_ENABLE_WORKSHOP`      | ON      | OFF  |
| `PHOENIX_ENABLE_GM`            | ON      | OFF  |
| `PHOENIX_ENABLE_MULTISHIP`     | ON      | OFF  |
| `PHOENIX_ENABLE_MODS`          | ON      | OFF  |
| `PHOENIX_BUNDLE_TEST_MISSIONS` | ON      | OFF  |

Workshop and GM are independent. Multi-ship controls additional player ship hosts; same-ship remote crew, AI vessels and carried craft remain available. Test Missions is a child mod, so bundling it requires mods enabled. Base-game loading and verification remain available with mods disabled.

The browser launcher receives the same capabilities in generated `runtime-capabilities.json`. Disabled entry points are hidden and direct launch/admission attempts are refused. Browser pack checking uses a content-only verifier instead of the disposable Workshop gameplay host.

## Build and package

From a bootstrapped Windows checkout:

```powershell
./scripts/build.ps1 -Target native -Demo
./scripts/build.ps1 -Target web -Demo
./scripts/package-demo.ps1
```

Native builds use `build/demo-native`; browser builds use `build/demo-web` and consume verified packs from the demo native build. The demo preset is `cmake/demo.cmake`. `-Demo` selects that preset and builds only the required runtime targets. Dependency source downloads from an existing native build are reused when present; compiler outputs remain separate.

Packaging writes dated staging directories, SHA-256 manifests and ZIPs under `build/demo-packages`. The native executable is `PhoenixDemo.exe`; packaging includes the installed Visual Studio x64 C++ runtime DLLs for app-local loading. Use `-VCRuntimeDirectory` to select a different compatible redistributable directory. For native crew invitations, set the Launcher page URL to the address serving this browser bundle. Serve the browser package over HTTP/HTTPS; opening `index.html` as a local file does not provide browser runtime services. Packaging does not deploy either artifact.

Only Phoenix Base Game and required fonts, UI, audio and client companions are included. Workshop/GM tools, Test Missions, development packs and diagnostic executables are excluded. The preset uses `PHOENIX_STORAGE_NAMESPACE=-demo` to separate native data, browser menu storage, journals and blobs from the full distribution.

## Other feature combinations

Use a separate build directory and ordinary CMake arguments, for example:

```powershell
./scripts/build.ps1 -Target native -BuildDirectory build/gm-only -ConfigureOnly -CMakeArgument @('-DPHOENIX_ENABLE_WORKSHOP=OFF')
./scripts/build.ps1 -Target native -BuildDirectory build/gm-only -BuildTarget native
```

Disable GM independently with `-DPHOENIX_ENABLE_GM=OFF`. A base-only distribution sets both `PHOENIX_ENABLE_MODS=OFF` and `PHOENIX_BUNDLE_TEST_MISSIONS=OFF`. `PHOENIX_PACK_INPUT_DIRECTORY` selects the verified native pack directory for a custom browser build. Set `PHOENIX_STORAGE_NAMESPACE` to a distinct lowercase suffix when distributions should not share saved data.

Configuration checks inspect final runtime dependency graphs and refuse disabled tool links. `phoenix_feature_tests` exercises synthetic content admission and accessible entry paths under the configured flags. Browser launcher tests cover independent switches, direct launch refusals and large-archive previews. Run `node tests/system/feature_network.mjs` after building `phoenix_feature_tests` for the explicit same-ship crew admission check against a temporary loopback signalling service. These checks do not establish physical-device, screen-reader or frame-rate acceptance.

See the [31 October 2026 verification receipt](demo-verification.md) for the packaged build evidence and remaining platform acceptance limits.
