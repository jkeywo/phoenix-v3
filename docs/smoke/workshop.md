# Workshop authoring smoke receipt

Date: 17 October 2026. Explicitly invoked during implementation.

- Native Release and all three WASM entry points build. The bounded native system suite and WASM content/authoring integration case pass through the production loader.
- Launched Workshop in Chromium at the requested 1920 by 1080 display resolution. Imported the independent one-pixel fixture PNG via the actual browser file chooser, created a reusable entity definition, placed an instance by double-clicking the map, and added a ship slot. Verification refused the empty allowed-ships list without closing the editor, then passed after choosing the authored ship.
- Exported a verified ZIP via the browser download path. The native verifier loaded that exact download, reporting one entity, one map and its archive SHA-256 `07da51d6f0b2306a5d42d0802d37ef6d50c6944cfa418b6f43a8e5f668b5b406` (receipt only, not a golden expectation).
- Native Workshop loads that browser-authored pack through `--workshop-pack` and captures its map/instance/properties. Local capture/logs are under ignored `build/native/Release/`.
- Automated authoring case imports a PNG, creates/places/duplicates entities, saves/reloads map data, exports/reopens a self-contained pack and preserves the draft after an invalid map load. It also authors a separately named mod derived from a real base dependency and verifies the ordered pair in both native and WASM.

This establishes basic authoring/file interoperability. It does not establish live game loading, peer pack transfer, final 3D billboards, running console bindings, actual screen readers or six-screen performance. Native OS file-dialog interaction and the console layout editor still need representative manual use; their compilation is not recorded as a passed usability run.

## Native Courier map authoring

Date: 18 October 2026. Explicitly requested keyboard-and-mouse Workshop journey.

- Found and fixed two input blockers: the player presentation layer consumed the standalone tools' text queue, and shortcuts pressed/released between frames were lost by held-key polling. Workshop/GM now retain their text input and consume buffered key presses. Escape stays with the active tool.
- Using only the native Workshop UI, added `Courier Among Three Asteroids` (`phoenix-base.map1`) to the base pack, placed exactly three asteroid instances around the origin, and added one player slot named Courier at `(0, 0)`. Only `training.survey` is allowed in that slot. Inspected its Courier sprite and enabled engine, steering and radar systems; these are all systems currently exposed by the editor.
- Named the Courier template through its display-name field. Ctrl+A, paste, slot naming and Ctrl+S worked after the input fixes. Saved through the real Windows Save As dialog to the local, ignored `build/courier-asteroids.zip`; subsequent Save reused that destination.
- Removed the temporary input diagnostic, rebuilt and restarted the native application, then reopened that exact ZIP using File → Open pack. Selected the new map and confirmed the three asteroid entries, their arrangement and the sole Courier-only slot survived. Workshop Verify passed before saving and after reopening.
- Native Release and WASM Workshop builds passed. Existing `system.input-and-semantics` and `system.file-imports` checks passed. The authored ZIP is a manual receipt artifact, not an automated fixture or production-content golden.

This adds evidence for native text editing, Windows pack open/save dialogs and map/slot persistence. It does not establish a played session or the console layout editor's usability.
