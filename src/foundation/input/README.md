# Logical input routing

`input.cppm` exports `phoenix.input`; link `phoenix_input`. ConsoleRegion/InputRouter route pointer and logical controls to explicit Console IDs. ManualFlightInput tracks source-specific held axes and cancellation, so an interrupted source cannot silently regain authority when another source releases.

The module knows logical coordinates, routes and devices, not raylib/SDL events, an Application or the authoritative World. Platform/device adapters supply events; Console and host adapters decide whether a routed proposal may execute.

Input/semantics and manual-flight/Controller integration cases protect capture, release, region changes and cancellation. Accessibility/device smoke remains separately invoked evidence. Preserve held-source identity and release ordering; central receipts identify which cases/platforms ran.
