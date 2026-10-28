# Native screen device settings

Open **Settings → Audio** on a native screen to select its named output, **System default**, or **Muted**, and use **Test sound** to identify the speakers. **Settings → Controllers** lists connected gamepads and offers **Pair by pressing a button**, **Unassign**, connection status and cancellation. Pairing stops when its panel closes or leaves Controllers. Console settings work in the lobby and during play; host settings configure the Viewscreen monitor.

Assignments belong to the physical monitor, rather than the ship, authored Console, desktop index or SDL connection number. A different Console on that monitor inherits its devices. Explicit virtual layouts use their absolute normalised layout filename and region route when the region does not correspond to a full physical monitor. Moving/renaming that layout file creates a different virtual-screen identity. Native monitor identity retains the platform adapter's existing discovery and fallback limitations.

Preferences save immediately in the local library's global profile, shared between content bases and separate from mission saves. Older library indexes default to System default and no controller. Failed preference writes appear in Settings and the accessible status; they are not reported as successful durable saves.

## Routing and interruption

One controller belongs to one screen, and each screen has at most one controller. Explicit assignment moves ownership and releases the previous route. Serial plus model identifies a controller when available; otherwise only a uniquely matching device path plus model is eligible for restoration. Names and connection indices never establish identity. Devices without reliable identities, conflicting remembered identities and indistinguishable controllers require pairing. Explicit pairing can distinguish them for the current connection, but does not fabricate a persistent identity.

The identifying press is consumed. Input resumes after a complete physical sample reports neutral buttons and axes. Disconnect removes active routing while retaining the saved choice. Monitor/Console route changes resolve the saved screen identity again; assignments do not grant gameplay authority.

Console UI feedback uses the originating screen's output. Music, sirens and ship/world feedback use the Viewscreen output once. Existing volume controls remain shared. A missing explicitly selected output is shown as disconnected and remains silent; it never falls back. System default deliberately follows the system's current default endpoint. Output changes release the old screen's playback; current loops can restart on the new endpoint without replaying missed shots or sirens.

Native playback uses a private miniaudio implementation from the pinned raylib source, with internal symbols, the existing pinned Vorbis decoder and one engine per active endpoint. Streaming decoders retain their source bytes. Mission one-shots remain bounded at sixteen, with eight UI-click voices per screen. The browser retains raylib playback and its existing device behavior. Operating-system screen-reader speech routing is unchanged.

## Verification

Synthetic cases extend `system.input-and-semantics` and `system.native-menu-library` for screen identity, controller ownership, reconnects, ambiguity, neutral gating, output selection, persistence and older preferences. `system.screen-audio` exercises the production native decoder/device/engine path against two test-only miniaudio null endpoints, including concurrent playback, shared-endpoint engines, mute, unavailable outputs and WAV/Vorbis playback.

The explicit `--gui-navigation-smoke` journey also visits Console Audio and Controllers, switches mute/default, opens pairing and closes Settings. It writes `build/screen-devices-console-audio.png` and `build/screen-devices-console-controllers.png`. Run it with an isolated `--data` directory; this journey does not prove physical speaker/controller operation.

Physical acceptance requires two monitors, distinct audio outputs and controllers: identify each output, pair both controllers, change Consoles, restart, reorder monitors and unplug/replug each device. Confirm silence while a selected output is missing, no cross-screen actions and no stuck held input. Check the same selections/status with a real screen reader. Automated/null-backend checks do not establish those physical results or six-display performance.
