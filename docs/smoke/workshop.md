# Workshop authoring smoke receipt

Date: 17 October 2026. Explicitly invoked during implementation.

- Native Release and all three WASM entry points build. The bounded native system suite and WASM content/authoring integration case pass through the production loader.
- Launched Workshop in Chromium at the requested 1920 by 1080 display resolution. Imported the independent one-pixel fixture PNG via the actual browser file chooser, created a reusable entity definition, placed an instance by double-clicking the map, and added a ship slot. Verification refused the empty allowed-ships list without closing the editor, then passed after choosing the authored ship.
- Exported a verified ZIP via the browser download path. The native verifier loaded that exact download, reporting one entity, one map and its archive SHA-256 `07da51d6f0b2306a5d42d0802d37ef6d50c6944cfa418b6f43a8e5f668b5b406` (receipt only, not a golden expectation).
- Native Workshop loads that browser-authored pack through `--workshop-pack` and captures its map/instance/properties. Local capture/logs are under ignored `build/native/Release/`.
- Automated authoring case imports a PNG, creates/places/duplicates entities, saves/reloads map data, exports/reopens a self-contained pack and preserves the draft after an invalid map load. It also authors a separately named mod derived from a real base dependency and verifies the ordered pair in both native and WASM.

This establishes basic authoring/file interoperability. It does not establish live game loading, peer pack transfer, final 3D billboards, running console bindings, actual screen readers or six-screen performance. Native OS file-dialog interaction and the console layout editor still need representative manual use; their compilation is not recorded as a passed usability run.
