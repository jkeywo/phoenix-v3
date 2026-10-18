# Browser main menu

The title is **Against the Stars**, using the same locally bundled UnifrakturCook Bold gothic font as the native menu. Buttons and other controls retain Lato. Font attribution and redistribution terms are in [the font directory](../content/fonts/README.md).

`index.html` opens a plain HTML menu with the native menu's ordered buttons, title, cascading Host/scenario/ship choices, Join Code forms, Settings and Mod Packs popups. It uses the same artwork and reads the same nine-slice borders from the sprite sidecars. Labels remain HTML text. Dialogs trap/restore focus; Escape closes a popup before returning through the columns. Narrow screens scroll horizontally. Reduced motion disables transitions. Exit explains when the browser requires the user to close the tab.

The landing page requests no role `.js`, `.wasm` or `.data` bundle until entry:

| Choice                                 | Runtime                 | Entry                                                |
| -------------------------------------- | ----------------------- | ---------------------------------------------------- |
| Host Game / selected scenario and ship | `server`                | Verified, stopped pre-game lobby                     |
| Saved Game                             | `server`                | Verified recovery lobby                              |
| Join Game (New Ship)                   | `server`                | Approval and available ship-slot selection           |
| Join Game (As Player)                  | `client`                | Approval, Console selection and individual readiness |
| Join Game (As GM)                      | `workshop`, GM mode     | Approval and inspection-ship selection               |
| Workshop                               | `workshop`, editor mode | Workshop                                             |

Explicit invitations bypass the HTML choices. Existing QR links (`type=client` or `type=workshop&mode=gm`, plus `code` and `service`) retain their format. Links may also use `role=ship|player|gm|editor`. Host links include `action=host`, `scenario`, `slot` and `ship`. Recovery links use `action=recover&save=...` for an origin-local save; use ZIP import between devices. Parameters grant no approval, credentials, readiness or authority. Scripts and assets retain the deployment directory, including Pages subpaths.

The shared C++ lobby protocol injects host-world creation/fleet preparation from a separate server composition library. Client and Workshop builds retain enforced dependency boundaries excluding simulation/Flecs. Setup verification, admission, countdown, replication and activation remain in C++. Late approval controls remain available after runtime activation.

The loaded runtime has no separate HTML navigation toolbar. After launch the Viewscreen fills the canvas and exposes only Settings over the world. Settings returns to the current display or explicitly quits to Main Menu; returning to a lobby requires a new host/join flow. Console and GM clients remain separate runtime roles.

## Content and persistence

The pack CLI and Workshop exports include `launcher.json`, a display preview generated from the production-resolved catalogue. The CLI also writes adjacent `.zip.menu.json` metadata containing the final archive SHA-256. The published training preview loads without its ZIP; the ZIP must match that identity when downloaded for hosting.

Mod Packs stages imported ZIPs, enabled order and removals in IndexedDB. C++ resolves the full ordered content and selected setup again at lobby entry. Preview metadata never establishes valid content. Older packs can expose direct authored slots; re-export inherited content through Workshop for a complete preview. ZIP entry counts, preview size and decompression are bounded. Archive-controlled paths are never extracted by the shell.

Settings persist volume, installed English, pseudo-localisation and reduced motion; defaults are English and 50%. Pack/save files and the menu index hydrate the WASM filesystem before `main`. Runtime changes, including managed saves, copy back to IndexedDB in transactions. Return to Menu waits for that copy; failed storage preserves the preceding stored data and reports the failure. Abrupt tab/process termination can interrupt a pending browser write. Peer journals and admission credentials retain their separate stores/profiles.

Build native content tooling/training first, then web. `scripts/build.ps1 -Target web` copies the verified native training ZIP/catalogue when available. CMake packages the HTML/CSS/module and standalone menu assets; Pages stages them alongside the three role bundles. `scripts/check.ps1` runs synthetic routing/preview checks with the native/WASM and real-transport gates. See [graphical evidence](smoke/browser-menu.md).
