# Preference storage

`phoenix.application.preference_store` applies audio settings, assembles settings tabs and persists current application preferences through the user library. The browser fallback retains its existing local storage keys and JavaScript persistence callbacks.

Portable preference data and localisation live in `application/preferences`. This module is application composition code; renderers use the portable localisation interface directly.
