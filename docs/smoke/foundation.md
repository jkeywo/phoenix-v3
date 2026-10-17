# Build foundation smoke receipt

Date: 17 October 2026. Explicitly invoked during the implementation request. This is a compatibility check, not a gameplay or performance acceptance run.

- Windows native Release built using MSVC 19.38 and Windows SDK 10.0.22621.
- `native.exe --build-check`: real Flecs world creates and advances its clock to tick 1.
- `native.exe --smoke`: six-frame RmlUi home capture, clean exit; inspected the framebuffer image.
- `native.exe --workshop --smoke`: six-frame ImGui tool windows/map grid capture, clean exit; inspected the framebuffer image. Docking is enabled; drag/drop usability is not established by this capture.
- Emscripten 6.0.12 builds server/client/Workshop WASM, JS and font data bundles. Node runs the isolated WASM Flecs build check successfully.
- Served `build/web` at localhost and launched each role in the Codex Chromium in-app browser. All render. Clicked New game in the server build and observed the C++ server lobby screen. Workshop renders its tool panels; its initial window positions are intended for desktop dimensions.
- Initial failures fixed before acceptance: static FreeType target resolution, ImGui triangle culling, RmlUi block layout, and insufficient default WASM stack during Flecs bootstrap. WASM now reserves 1 MiB of guarded stack.

Local logs and native framebuffer images reside under ignored `build/native/Release/`. Browser checks used a narrow panel and establish startup, not final phone layouts. Lobbies are navigation shells; no network connections or mission selection exist yet. The simulator currently supplies only an ECS clock for the build proof.

Still pending: gameplay, authoring, mod packs, input assignment, screen readers, HarfBuzz shaping/IME, six-screen performance and networking/replication. No physical device, full accessibility or 60 Hz claim follows from these runs.

Follow-up semantic foundation: browser accessibility tree exposes C++-generated navigation buttons. Invoking New game through that tree reaches the C++ server lobby; toggling pseudo-localisation updates both graphic and semantic labels. Inspected narrow-screen expanded labels after fixing wrapping. The Windows `--platform-check` passes its real provider label/Invoke/action-queue probe. Neither check uses an actual screen reader. The Windows provider currently exposes the navigation group, and still needs accurate per-control bounds/focus and complete player-widget coverage.

Final focus check: selected a graphical C++ button, returned Home through its semantic action, then typed a Space in the HTML network-code field. The field received the text and the C++ screen stayed Home. Graphics keyboard input now requires actual canvas/window focus, loss of focus clears graphical held keys, and semantic HTML controls precede the canvas in keyboard order. Full-width navigation was inspected at 1080p after removing content-dependent shrinking. The native UIA programmatic probe still passes.
