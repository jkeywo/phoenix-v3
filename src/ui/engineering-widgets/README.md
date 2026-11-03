# Permitted machinery cards and controls

Start with [engineering.cppm](engineering.cppm).

Card/control assembly is stateless and consumes only the supplied Observation. Live resource/fault state remains authoritative elsewhere.

Power settings index the current observation's enabled Controller channels once per build and borrow the published limits while producing the ordered settings list.

Dependencies: Command/observation contracts and localisation; no Application or renderer.

Validation: Existing power-panel/controller integration cases and native/browser builds.
