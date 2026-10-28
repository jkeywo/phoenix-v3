# Preference storage

`phoenix.application.preference_store` applies audio settings, assembles settings tabs and persists current application preferences through the user library. The browser fallback retains its existing local storage keys and JavaScript persistence callbacks.

Portable preference data and localisation live in `application/preferences`. This module is application composition code; renderers use the portable localisation interface directly.

`screen_devices.cpp` owns monitor-keyed device preferences, safe controller recognition and restoration, one-to-one assignment, pairing/neutral admission and device-setting actions. The native adapter supplies screen identities and connection metadata; playback receives only the selected output and originating screen. Device choices are stored in the global profile, independently of content bases and mission saves.
