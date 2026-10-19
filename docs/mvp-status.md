# MVP status — 19 October 2026

This is the current status of the approved architecture MVP, not completion of the full GDD. Older delivery notes in the implementation record describe their state at delivery.

The subsequent [comic rendering increment](comic-rendering.md) adds calibrated per-pixel sprite depth, visual occlusion, depth-backed sun shadows, hatching, propulsion lighting and shared Workshop previews. It upgrades the training art and adds `training.lighting`; explosion events remain internal and untriggered.

The [Console Controller delivery record](controller-refactor-plan.md) tracks the subsequent control refactor. Human and AI operators share admitted command sections and permitted observations; complexity profiles select operators, physical Systems consume bindings and report facts, and public messages use the entity board. Save schema 6 and wire schema 5 refuse the old authority format.

| Area                      | Implemented behavior                                                                                                                                                                    |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Entry points              | Native server/client/Workshop/GM and separate server, client and Workshop WASM applications; shared static libraries and guarded authority boundaries.                                  |
| Content                   | Verified ordered ZIP packs, inheritance, explicit overrides, dependencies, exact hashes, browser/native import/export and persistent verified caches.                                   |
| Play                      | Mission/ship-slot/ship/Console selection; billboards, radar, engine, steering, collisions and serialisable delegated controllers.                                                       |
| Fleet                     | One complete deterministic world per fixed server member, quorum commitment, durable identities, absence/return, checkpoints and snapshot/suffix catch-up.                              |
| Crew and GM               | Approval before content transfer, per-Console leases and permitted views, disconnected-seat replacement, read-only GM inspection and QR invitations.                                    |
| Transfers and saves       | Bounded content and projection chunks; portable browser/native recovery bundles preserve content/state and create fresh transport credentials without live claims.                      |
| Displays and input        | One Viewscreen plus independent Console regions in one spanning window; captured pointer routes, SDL/browser controller assignments and explicit keyboard/accessibility focus.          |
| Player text/accessibility | Shared semantic controls, browser native editable inputs, Windows UIA invocation/focus/bounds/Unicode Value editing, native action notifications, HarfBuzz/FreeType and fallback fonts. |
| Authoring                 | Docked map/list/properties, entity/System/collision/render settings, ship slots, explicit dependency overrides and live isolated RmlUi Console previews.                                |

English and pseudo-localisation are the current UI language baseline. Shared resources live in `content/locales.json`; static tool labels retain stable ImGui IDs across translation. Native and browser Arabic/Hindi samples render through the shaping engine. Browser Unicode editing reaches the C++ field, and native UIA Unicode editing passes the platform probe. This is not certification of mixed-direction editing, a complete Arabic/Hindi translation, or ImGui complex-script shaping. Additional launch languages remain a separate product choice.

## Evidence and remaining manual checks

- The synthetic automatic gate covers native/WASM systems and real signalling, transport, crew, GM, replacement, fleet partitions and checkpoint recovery. See [building](building.md) and [testing](testing.md).
- A real native/native/WASM fleet advanced together, recovered its browser member from strict IndexedDB, accepted browser engine/steering requests, and replicated the resulting motion to a native world. A separate web crew used forced application relay; approved web GM inspection showed the same live ship. The browser exported a recovery bundle at tick 10019; the production loader verified all three fixed members, fresh epoch/key and cleared claims. See [network evidence](smoke/network-play.md).
- Manual captures exercise the actual three-monitor layout and a 5760×2160 rendering workload with one Viewscreen, five claimed Consoles and populated radars. See [display evidence](smoke/bridge.md). The latter renders six 1080p regions; only three physical monitors were available.
- Windows enumerated an Xbox One controller. Physical button presses remain a manual acceptance item; five simultaneous physical controllers were unavailable. Touchscreen mapping and physical touch checks are deferred at the user's request.
- Audible reader checks were skipped at the user's request. Programmatic UIA/DOM checks establish the exposed semantics, not a completed screen-reader user journey.
- Physical phone-camera QR scanning, off-machine NAT traversal and public WSS hosting are not established by loopback smoke runs. GitHub Pages hosts the browser binaries; the signalling service requires its own host.

These limits remain visible acceptance items. They are not silently counted as passes.

## Repeatable manual tools

```powershell
# Generate verified synthetic content and a six-region layout.
python tests/manual/bridge_fixture.py

# Use the actual desktop arrangement (first display is the Viewscreen).
build/native/Release/native.exe --local-only --span-displays `
  --game-pack build/bridge-smoke/bridge.zip --mission test.map --slot test.slot --ship test.ship `
  --performance-output build/bridge-smoke/three-displays.json --performance-frames 1800

# Render the six-region workload; inspect active_consoles and actual framebuffer dimensions.
build/native/Release/native.exe --local-only --display-layout build/bridge-smoke/six-regions.json `
  --game-pack build/bridge-smoke/bridge.zip --mission test.map --slot test.slot --ship test.ship `
  --performance-output build/bridge-smoke/six-regions-result.json --performance-frames 1800

# Native shaping/input surface; browser equivalent: ?type=server&textprobe=1.
build/native/Release/native.exe --local-only --text-probe
build/native/Release/native.exe --platform-check
```

Display-layout runs use uncapped rendering by default. `--frame-limit N` and `--vsync` allow explicit pacing comparisons; `--uncapped` is also available. On this reference machine the 60 fps software limiter caused periodic graphics-flush stalls; uncapped rendering stayed inside the 16.7 ms workload budget. Captures report CPU wall time and presentation separately; they do not claim isolated GPU timing or six physical displays.
