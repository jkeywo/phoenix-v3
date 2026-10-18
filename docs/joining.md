# Join links and QR codes

The native and [HTML browser](browser-menu.md) main menus separate joining as a new ship server, player or GM. Each opens Join Code and Connect, with service URL and local profile under Advanced Connection Settings. [Pre-game lobbies](native-menu.md) approve participants, arbitrate ship slots, transfer exact setup automatically and coordinate readiness. Player/GM forms also accept direct running ship-server codes. Joining replaces the previous connection only after validating connection options. Explicit QR/launch parameters skip the HTML choices and load the player or GM WASM build directly. The shared lobby protocol preserves direct running-server discovery, stored client credentials and host approval.

In a native or browser ship server's Home/Server lobby, enter the launcher page URL and choose Show console QR or Show GM QR. The QR and copied link contain only role/mode, server code and service URL. Scanning opens the ordinary launcher and grants no operating or inspection permission. The source page URL must have no query, fragment, userinfo or whitespace. Both the page and service must be reachable by the joining device; a loopback URL works only on the machine hosting it. Use HTTPS and WSS for a real deployment. No service is publicly deployed automatically.

Browser hosts prefill their own launcher page address. Native accepts `--page https://your-host/index.html`; a page can also be entered in its C++ screen. Each separately operated client on one machine/origin should use a distinct local profile. QR links intentionally omit stored client credentials and profile identity.

QR generation uses the pinned MIT-licensed C++ Nayuki library. The dependency's revision/hash are in CMake, and its notice ships in `assets/QR-LICENSE.txt`. The code is generated in C++ and drawn as one cached texture with a four-module quiet zone and integer module sizing; no JS screen implementation is involved.

Manual smoke captures:

```powershell
./build/native/Release/native.exe --qr-smoke --code your-room/host --page http://127.0.0.1:8083/
./build/native/Release/native.exe --join-smoke --code your-room/host
```

The QR capture writes `invitation-smoke.png`; the join capture writes `network-console-smoke.png` after the production RmlUi action connects, claims and renders a permitted console. These are manually triggered tools, outside the automatic gate. The join fixture must have an empty station.

Recorded development evidence: an actual native framebuffer QR independently decoded with ZXing-C++ to the expected role, room/server and service query parameters. The native form/claim capture uses a separately running synthetic production host. A browser host rendered GM and console invitation QRs and confirmed completion of its clipboard promise through the C++ status path, with no browser errors/warnings. Copy failures are reported instead of claiming success when merely queued. This establishes rendering/decoding and the form path, not a physical phone-camera or public-network smoke test. Native form screen-reader text-entry support remains part of platform acceptance work.
