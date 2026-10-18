# Native menu and pre-game lobby

The native application opens with Host Game, Join Game (New Ship), Join Game (As Player), Join Game (As GM), Workshop and Exit. Settings is at the top left; Mod Packs is at the bottom left. Earlier choices remain in columns to the left. Back or Escape closes an open dialog, then returns through the choices. Long lists scroll, and the current column is brought into view. The menu uses the configured Viewscreen region, the first Console region, or the ordinary window.

The menu uses the [comic HUD artwork and sprite sidecars](../content/ui/menu/README.md): scalable buttons, a scalable popup backing, a generated settings cog and a generated bridge background. RmlUi reads source borders and label markers from the JSON; button corners keep their proportions as columns and localised labels resize. Text, focus, hit targets and accessibility remain live controls.

## Preparing a game

Import gameplay ZIPs in Mod Packs, enable them in dependency/override order, and Apply Pack Selection. Installation and activation are separate: an imported pack starts disabled. Packs remain installed between launches. Failed verification preserves the previous selection. Leave an active game before applying another selection. Removing an entry and applying removes its installed archive; portable saves retain their own exact content.

Host Game shows New Game followed by managed Saved Games and Import Save. New Game shows resolved scenarios, then each ship slot with its allowed ships. Selecting a ship opens a stopped lobby. It does not start simulation or claim a human Console.

Share the lobby Join Code. New Ship participants request approval, then choose an available slot and allowed ship; the coordinator arbitrates simultaneous requests. Approved ship servers receive exact packs and a private, hash-verified setup automatically. Players request approval, select a ship/Console and ready individually. GMs select an inspection ship after approval and do not participate in readiness. Advanced connection settings retain service URL and local profile. New native player/GM forms also accept existing direct ship-server codes, including browser-hosted games.

A ship host may select a local Console and ready as a player. Without a local Console, a ship host can ready an AI-only ship when no remote crew are connected to it. Every approved crew player must select a Console and ready. Pending requests and GMs do not count. The coordinator can remove unused or disconnected pre-game participants.

Everyone ready begins a five-second countdown. Unreadying, membership changes, disconnects, or ship/Console changes cancel it. Changed setup must be verified again before another countdown. At completion the server roster freezes, and the existing durable fleet activation barrier starts the game. Every server simulates the complete world and its authored AI deterministically. Human claims suppress delegated control through the ordinary ownership rules.

Unclaimed slots marked `mandatory` use their first allowed ship as AI. Unclaimed optional slots remain empty. AI-only ships have no server identity, vote or fictitious participant. New ship servers cannot join after launch; returning fixed-roster members and ordinary late crew/GM admission retain their existing paths. The [HTML browser menu](browser-menu.md) gathers the same setup choices before loading its role WASM; both use the shared production lobby protocol and preserve direct running-server invitations.

A single-screen native host selects Viewscreen, Player console or GM inspector in the pre-game lobby. Existing native display layouts keep their configured separate roles. Browser hosts open the Viewscreen; player/GM clients join separately. After launch, the Viewscreen contains only the world and Settings. Consoles and GM tools also open Settings instead of returning to a lobby. **Quit to Main Menu** ends the native session; starting/joining again is a new game flow. Closing Settings restores the same display.

## Saves and settings

Native Windows defaults to `%LOCALAPPDATA%\PhoenixV3`, independent of the launcher's working directory. Use `--data <directory>` to override it, including `--data userdata` to continue using an older working-directory library. The selected directory contains `menu/library.json`, installed ZIPs under `menu/packs/`, and managed saves under `menu/saves/`, separate from consensus journals and credentials. A fresh library installs and enables the bundled Phoenix Base Game pack automatically. Save Game in in-game Settings stores a portable save after a tick boundary. Export Save retains the external ZIP route. Import Save verifies through a candidate session before replacing the current game. Managed entries show scenario, name and UTC save time. Recovery preserves the saved world and ship roster, waits for its server slots, and uses readiness/countdown to resume with fresh transport identities.

Settings remembers music volume, installed-language selection, pseudo-localisation and reduced motion. First-launch defaults are 50% volume and English. Closing Settings restores the previous screen and menu choices; settings do not pause simulation. English is currently the installed language. Pseudo-localisation exercises expansion without claiming another language is supported.

The visible menu and Windows UI Automation share controls, text/range values, and lobby readouts. Disabled background buttons are excluded while dialogs are open. Keyboard/controller navigation and dialog focus use the same RmlUi actions. Native reader usability and physical controller operation still require device smoke evidence.

## Validation

`system.native-menu-library` checks durable preferences, exact installed archives, transactional replacement and managed save recovery with synthetic content. Native/WASM fleet setup checks cover mandatory AI slots, omitted optional slots and continued deterministic AI. The signalling-service gate also runs the synthetic native lobby journey over real relay transports: authenticated approval, competing slot requests, automatic setup transfer, individual readiness, countdown cancellation, durable launch, GM/Console admission and portable fleet recovery.

Explicitly trigger the graphical journey with:

```powershell
./build/native/Release/native.exe --local-only --game-pack build/native/training.zip --data build/menu-smoke-data --menu-smoke
```

This writes `build/menu-native-*.png` and exercises native RmlUi Tab traversal, UIA range/readout contracts, pack selection, the readiness countdown and managed saving from in-game Settings and explicit quit to Main Menu. It is manually triggered development evidence, separate from automatic CI and actual reader/device acceptance. See the [validation receipt](smoke/native-menu.md).
