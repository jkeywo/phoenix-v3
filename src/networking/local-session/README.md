# Authoritative local execution

Start at `local.cppm` and `execution.cppm`.

Owns local host sessions, committed-frame execution and the shared simulation worker session adapter. HostAccess is server-only. FrameSession coordinates the worker without moving physical rules into networking.

Allowed dependencies: Session API and authoritative simulation; browser worker service calls use platform lifecycle.

Ownership and lifetime: Stop/join the simulation worker before destroying the owned session/World. Persistence uses the existing portable session format and committed frame order.

Relevant validation: `system.local-session`, Controller/system continuation cases, ordinary/threaded browser gates. These identify checks to run; this README is not execution evidence.
