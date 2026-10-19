# Native menu and pre-game lobby

The native application opens with Host Game, Join Game (New Ship), Join Game (As Player), Join Game (As GM), Workshop and Exit. Settings is at the top left; Mod Packs is at the bottom left. Earlier choices remain in columns to the left. Back or Escape closes an open dialog, then returns through the choices. Long lists scroll, and the current column is brought into view. The menu uses the configured Viewscreen region, the first Console region, or the ordinary window.

The menu uses the [comic HUD artwork and sprite sidecars](../content/ui/menu/README.md): scalable buttons, a scalable popup backing, a generated settings cog and a generated bridge background. RmlUi reads source borders and label markers from the JSON; button corners keep their proportions as columns and localised labels resize. Text, focus, hit targets and accessibility remain live controls.

## Preparing a game

The fixed cockpit frame overlays a slowly panning, horizontally repeating space panorama. Reduced motion stops the pan. A ceiling-mounted CRT in the upper right displays the saved player avatar and username; the lobby reuses its housing for QR invitations. The scenery and screen aperture share source-pixel metadata with the HTML menu. See [menu artwork](../content/ui/menu/README.md) for assets and generation prompts.

Import gameplay ZIPs in Mod Packs, enable them in dependency/override order, and Apply Pack Selection. Installation and activation are separate: an imported pack starts disabled. Phoenix Base Game and Phoenix Training are separate bundled packs; fresh libraries enable only the base. Every pack can be toggled, including the base. Enabling a mod enables and orders its prerequisites; disabling a prerequisite disables dependent mods. An entirely disabled selection persists across restarts. Packs remain installed between launches. Failed verification preserves the previous selection. Leave an active game before applying another selection. Removing an entry and applying removes its installed archive; portable saves retain their own exact content.

Host Game shows New Game followed by managed Saved Games and Import Save. New Game shows resolved scenarios, then each ship slot with its allowed ships. A sole scenario is selected automatically; a sole slot with one allowed ship goes straight to the stopped lobby. Multiple scenarios, slots or ships still require a choice. Back skips any omitted scenario step. Native and browser menus use the same behavior. Entering the lobby does not start simulation or claim a human Console.

Share the lobby Join Code. New Ship participants request approval, then choose an available slot and allowed ship; the coordinator arbitrates simultaneous requests. Approved ship servers receive exact packs and a private, hash-verified setup automatically. Players request approval, select a ship/Console and ready individually. GMs select an inspection ship after approval and do not participate in readiness. Advanced connection settings retain service URL and local profile. New native player/GM forms also accept existing direct ship-server codes, including browser-hosted games.

The host's main screen is always the selected ship's Viewscreen. The lobby lists authored Consoles, who has claimed each one, and their readiness. Players choose an available Console and ready there. When no remote crew are connected, the host can ready an AI-operated ship. Pending requests and GMs do not count. The coordinator can remove unused or disconnected pre-game participants.

Native hosts can put an unclaimed Console on a named additional monitor, or assign the GM inspector to one. The host's monitor remains reserved for the Viewscreen. Assigning a local Console reserves it against remote claims and creates a local player with a separate name, uploaded avatar and Ready control. A screen can have one role, and a Console can appear on one local screen. Launch applies the assignments to the existing spanning-window renderer; quitting restores the menu window. This uses detected monitors, up to six total roles. Explicit CLI display layouts remain supported.

Everyone ready begins a five-second countdown. Unreadying, membership changes, disconnects, or ship/Console changes cancel it. Changed setup must be verified again before another countdown. At completion the server roster freezes, and the existing durable fleet activation barrier starts the game. Every server simulates the complete world and its authored AI deterministically. Human claims suppress delegated control through the ordinary ownership rules.

Unclaimed slots marked `mandatory` use their first allowed ship as AI. Unclaimed optional slots remain empty. AI-only ships have no server identity, vote or fictitious participant. New ship servers cannot join after launch; returning fixed-roster members and ordinary late crew/GM admission retain their existing paths. The [HTML browser menu](browser-menu.md) gathers the same setup choices before loading its role WASM; both use the shared production lobby protocol and preserve direct running-server invitations.

Native and browser hosts open the selected ship's Viewscreen; player/GM clients join separately. After launch, the Viewscreen contains only the world and Settings. Consoles and GM tools also open Settings instead of returning to a lobby. **Quit to Main Menu** ends the native session; starting/joining again is a new game flow. Closing Settings restores the same display.

The host shows a QR invitation and a six-character uppercase alphanumeric Join Code. Names and uploaded avatars are editable in the join form, lobby or Settings. The local application retains its chosen name and avatar; names appear with the participant's role and claim/readiness state. Avatar images are cropped and resized to 48 by 48 PNGs before storage and bounded transfer. Profile images do not carry operating authority.

Assigning a Console to a native screen applies the display layout immediately. That monitor shows its Console artwork and an authored name/description card with Ready and Back. Ready changes the corresponding local player's readiness; Back removes that assignment without leaving the host lobby. Controls behind the card are previews until the game starts. The same display becomes live after the countdown.

## Saves and settings

Native Windows defaults to `%LOCALAPPDATA%\PhoenixV3`, independent of the launcher's working directory. Use `--data <directory>` to override it, including `--data userdata` to continue using an older working-directory library. The selected directory contains `menu/library.json`, installed ZIPs under `menu/packs/`, and managed saves under `menu/saves/`, separate from consensus journals and credentials. A fresh library installs and enables the bundled Phoenix Base Game pack automatically. Save Game in in-game Settings stores a portable save after a tick boundary. Export Save retains the external ZIP route. Import Save verifies through a candidate session before replacing the current game. Managed entries show scenario, name and UTC save time. Recovery preserves the saved world and ship roster, waits for its server slots, and uses readiness/countdown to resume with fresh transport identities.

Settings is organised into **Audio**, **Profile**, **Language** and **Display** tabs. During play, **Session** holds approvals, Save Game, Export Save and Quit to Main Menu. Only the active tab exposes its controls; changing tabs retains settings and menu choices. Tab/Shift+Tab traverses controls, while Left/Right and Home/End switch tabs when a tab has focus. The last tab remains selected while the application is open.

Settings remembers Master, Music and SFX volume separately, along with installed-language selection, pseudo-localisation and reduced motion. All three volume sliders start at 50%; existing music preferences are retained when upgrading. Master multiplies both category levels, so either Master or SFX at zero silences button clicks. Main-menu, popup and lobby buttons reuse V2�s `ui_click.ogg`, with a fresh 0.94�1.06 pitch multiplier per activation; keyboard, controller and accessibility actions share the same route. Audio is local presentation and never consumes simulation randomness. English is the initial language. Closing Settings restores the previous screen and menu choices; settings do not pause simulation. English is currently the installed language. Pseudo-localisation exercises expansion without claiming another language is supported.

The visible menu and Windows UI Automation share controls, text/range values, and lobby readouts. Disabled background buttons are excluded while dialogs are open. Keyboard/controller navigation and dialog focus use the same RmlUi actions. Native reader usability and physical controller operation still require device smoke evidence.

## Validation

`system.native-menu-library` checks durable preferences, exact installed archives, transactional replacement and managed save recovery with synthetic content. Native/WASM fleet setup checks cover mandatory AI slots, omitted optional slots and continued deterministic AI. The signalling-service gate also runs the synthetic native lobby journey over real relay transports: authenticated approval, competing slot requests, automatic setup transfer, individual readiness, countdown cancellation, durable launch, GM/Console admission and portable fleet recovery.

Explicitly trigger the graphical journey with:

```powershell
./build/native/Release/native.exe --local-only --game-pack build/native/training.zip --data build/menu-smoke-data --menu-smoke
```

This writes `build/menu-native-*.png` and exercises native RmlUi Tab traversal, UIA range/readout contracts, pack selection, the readiness countdown and managed saving from in-game Settings and explicit quit to Main Menu. It is manually triggered development evidence, separate from automatic CI and actual reader/device acceptance. See the [validation receipt](smoke/native-menu.md).

The profile TV opens the name/avatar editor. Twelve generated naval crew portraits are bundled; a new profile chooses one at random. The host lobby replaces that TV with its QR invitation at the same size and position. Crew, Consoles and GM tabs keep normal lobby content compact. Each assigned local Console has its own profile TV and a waiting card with console name, description and Ready; it has no Leave/Back control. Remote assigned players retain Leave Game. Authored gameplay widgets stay hidden until play starts.
