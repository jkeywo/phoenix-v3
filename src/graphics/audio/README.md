# Presentation audio policy and playback

Start with [audio.cppm](audio.cppm).

ServerMusic owns playback/device resources behind a private State. AudioPresentation is assembled by the caller and grants no session access. audio_policy.cppm exposes the device-independent mixer and revision transition contract.

Dependencies: Content/scene projections and raylib facilities. Browser playback uses raylib; native playback uses `native_music.cpp` and the [native output owner](native_backend.cppm), `phoenix.graphics.audio_backend`. The native owner hides miniaudio implementation and voice data, shares one engine per output endpoint, and consumes the pinned Vorbis decoder. Its integration test imports the same owner and selects synthetic null endpoints. UI requests carry their originating screen identity. The policy target does not link graphics/device libraries.

Validation: Existing combat host-audio and controller-runtime alert cases, plus `system.screen-audio` through two synthetic null endpoints. Actual devices remain manual smoke; see [screen device evidence](../../../docs/smoke/screen-devices.md).
