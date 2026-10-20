# Comic messages — 20 October 2026

The message component uses recovered non-uniform portraits and Comic Neue Regular. Native captures were inspected at 900×700 and 360×740, including enlarged text, long wrapped prose, escaped markup and Latin/Arabic fallback glyphs. Explicit scrollbar dimensions prevent RmlUi's unstyled scrollbar from consuming the narrow message area.

The manual probe checks separate vertical rows, usable narrow width, matching accessible text, retained element identity, focus and scroll after a new message. Final native probe: passed. Captures and build logs are in the ignored `build/message-assets` directory.

Native verification used the isolated `build/messages-native` directory because other work was building in the usual shared directory. The native application and message probe built successfully. CTest passed all three selected suites: `system.gameplay_knowledge`, `system.controller_runtime` and `system.local-session`. The synthetic message scenario includes a real PNG profile snapshot, delayed delivery across save/restore and operator handover, and private-message exclusion from the public presentation.

Browser client, server and the message probe built successfully. Both WASM gameplay-knowledge and controller-runtime integration executables passed under Node. The browser probe also passed in the in-app browser; its wide and narrow captures were inspected, including the font, portraits, long wrapping and multilingual fallback glyphs. Arrival retained focus and scroll. A transient concurrent pack-loading compile error was corrected before the successful build.

Actual screen-reader operation, physical touch/gamepads and six-display performance remain separate acceptance work. These checks do not establish those results.
