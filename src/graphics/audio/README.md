# Presentation audio policy and playback

Start with [audio.cppm](audio.cppm).

ServerMusic owns playback/device resources behind a private State. AudioPresentation is assembled by the caller and grants no session access. audio_policy.cppm exposes the device-independent mixer and revision transition contract.

Dependencies: Content/scene projections and raylib playback. The policy target does not link graphics/device libraries.

Validation: Existing combat host-audio and controller-runtime alert cases; actual audio remains manual smoke.
