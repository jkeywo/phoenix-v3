# Physical device adapters

Start at `devices.cppm` (`phoenix.devices`).

Translates native SDL/touch or browser events into device/control events. Native Added events include a friendly name, hardware serial, device path and model GUID alongside the connection-local ID. Unmapped buttons can identify a controller, and a complete physical neutral sample gates newly assigned input. Console assignment, region routing and command authority remain above this adapter.

Allowed dependencies: Native SDL/Windows pointer services or browser APIs. No simulation or content dependency.

Ownership and lifetime: Release pending/held input when devices disconnect or assignments change. An OS device index alone is not a permanent player identity.

Relevant validation: `system.input-and-semantics`; simultaneous physical touch/five-controller acceptance remains manual. These identify checks to run; this README is not execution evidence.

# Stable display identity

`phoenix.platform.display_identity` is a separate small target in this folder. It resolves a monitor's stable Windows identity from its position and falls back to the supplied name elsewhere. It owns no window, SDL device, assignment or application state.
