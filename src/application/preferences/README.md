# Shared preferences and localisation

`phoenix.application.preferences` owns the installed translation catalogue,
selected language and bounded per-thread translation cache. Its interface is
`preferences.cppm`; the implementation is `preferences.cpp`.

`MenuPreferences` is also declared here as portable data. The user library owns its persistence; `application/preference-store` applies and stores current application settings.

Consumers provide translation resources at application setup, select a language,
and resolve labels through the shared functions. The module depends only on
portable data contracts. It has no renderer, session, device or `Application`
dependency. Existing pseudo-localisation and catalogue validation behaviour is
preserved. Configuration remains application-owned and follows the existing
startup/main-thread lifetime.

Relevant checks are the input/semantics, menu and content system cases, plus
player/tool localisation smoke journeys when explicitly invoked.
