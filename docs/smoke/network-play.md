# Network play smoke evidence

17 October 2026, local development machine, loopback service, release native and Emscripten builds.

- Native synthetic host to native client: verified archive transfer, durable station claim, production engine/steering execution, out-of-range radar exclusion and wrong-ship command refusal passed.
- Native synthetic host to native GM: full inspection appeared only after host approval; operating requests remained refused.
- Disconnected client to replacement client: initial claim required explicit confirmation; confirmed replacement moved the ship; the previous identity had no observation or control. This journey now runs in the automatic service integration gate.
- Native synthetic host to browser client: real HTML launcher, persistent IndexedDB profile/cache, semantic claim action, speed/heading controls and spoken radar-contact button label worked. Reload recovered the existing client profile and renewed its claim. Browser console diagnostics contained no errors or warnings.
- Native synthetic host to browser GM: approved docked map, full entity list and read-only status rendered; browser diagnostics contained no errors or warnings.
- Browser ship server to browser console: the training ZIP was imported through the file chooser, mission started, and the remote console claimed with one semantic action. Speed/heading requests changed the permitted operating state and heading; the radar report contained the two permitted training contacts. The browser host advanced durable committed ticks, and its diagnostics contained no errors or warnings. This check found and corrected station-intent starvation during asynchronous tick writes; the automatic probe now makes one claim request rather than masking the race with repeated clicks.

## Mixed fleet — 18 October 2026

The manual `phoenix_browser_fleet_probe` prepared one exact synthetic three-member bundle. Two native processes loaded ship1/ship2; the browser imported that bundle, selected ship3 and started from its fixed identity. All replicas advanced. The browser member was closed and reloaded from its original IndexedDB profile while the native majority continued, then caught up and reclaimed its local Console. Engine/steering actions produced 850 mm/s and a 5000 mdeg heading in a native replica, with actual position changes. This exposed and fixed admission starvation caused by requiring a live follower to match every newly advertised leader commit after its incarnation barrier had already committed.

The browser ship released its local Console. A fresh web client using `relay=1` received content only after explicit host approval, claimed the seat and committed another speed/heading request. A native replica then showed heading 10000 mdeg and corresponding motion. A separate web GM using application relay was approved; its docked inspector listed all four synthetic entities and displayed the moving ship's read-only properties. Browser diagnostics contained no warnings or errors.

The browser exported its portable recovery ZIP at tick 10019. `phoenix_browser_fleet_probe --inspect` loaded it through the production fleet loader and verified three fixed members, a changed epoch/key, the original roster and no live claims. The original bundle, downloaded save, peer journals and process logs remain in ignored `build/bridge-smoke`; no private fleet key is committed.

These are same-machine application journeys. They establish the exercised mixed-platform fleet paths, not public NAT traversal, actual reader operation, physical touch/gamepads or six physical displays.
